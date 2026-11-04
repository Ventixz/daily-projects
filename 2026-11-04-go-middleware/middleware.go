// Package main: composable HTTP middleware in Go (stdlib only).
// A Middleware wraps a http.Handler; Chain applies them so the first listed is outermost.
package main

import (
	"fmt"
	"log"
	"net/http"
	"strings"
	"time"
)

type Middleware func(http.Handler) http.Handler

// Chain wraps h so that mws[0] runs first (outermost).
func Chain(h http.Handler, mws ...Middleware) http.Handler {
	for i := len(mws) - 1; i >= 0; i-- {
		h = mws[i](h)
	}
	return h
}

// statusRecorder captures the status code written by downstream handlers.
type statusRecorder struct {
	http.ResponseWriter
	status int
}

func (r *statusRecorder) WriteHeader(code int) {
	r.status = code
	r.ResponseWriter.WriteHeader(code)
}

// Logging logs method, path, status and duration through logf.
func Logging(logf func(string, ...any)) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			start := time.Now()
			rec := &statusRecorder{ResponseWriter: w, status: http.StatusOK}
			next.ServeHTTP(rec, r)
			logf("%s %s -> %d (%s)", r.Method, r.URL.Path, rec.status, time.Since(start))
		})
	}
}

// Recover turns a panic in a downstream handler into a 500 response.
func Recover(logf func(string, ...any)) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer func() {
				if err := recover(); err != nil {
					logf("panic: %v", err)
					http.Error(w, "internal server error", http.StatusInternalServerError)
				}
			}()
			next.ServeHTTP(w, r)
		})
	}
}

// BearerAuth requires "Authorization: Bearer <token>" with a token in tokens.
func BearerAuth(tokens map[string]bool) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			tok, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
			if !ok || !tokens[tok] {
				w.Header().Set("WWW-Authenticate", "Bearer")
				http.Error(w, "unauthorized", http.StatusUnauthorized)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}

// RateLimit allows at most n requests per window (global, fixed window). now is injectable for tests.
func RateLimit(n int, window time.Duration, now func() time.Time) Middleware {
	var (
		start time.Time
		count int
	)
	mu := make(chan struct{}, 1)
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			mu <- struct{}{}
			t := now()
			if t.Sub(start) >= window {
				start, count = t, 0
			}
			count++
			allowed := count <= n
			<-mu
			if !allowed {
				w.Header().Set("Retry-After", fmt.Sprint(int(window.Seconds())))
				http.Error(w, "too many requests", http.StatusTooManyRequests)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}

func NewServer(logf func(string, ...any)) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/hello", func(w http.ResponseWriter, r *http.Request) { fmt.Fprintln(w, "hello") })
	mux.HandleFunc("/boom", func(w http.ResponseWriter, r *http.Request) { panic("kaboom") })
	return Chain(mux,
		Logging(logf),
		Recover(logf),
		RateLimit(100, time.Minute, time.Now),
		BearerAuth(map[string]bool{"secret": true}),
	)
}

func main() {
	log.Println("listening on :8080 (token: secret)")
	log.Fatal(http.ListenAndServe(":8080", NewServer(log.Printf)))
}
