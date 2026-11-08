// Package main: composable net/http middleware (request ID, logging,
// panic recovery, token-bucket rate limiting, bearer auth).
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

// Middleware wraps a handler.
type Middleware func(http.Handler) http.Handler

// Chain applies middleware so the first listed is the outermost.
func Chain(h http.Handler, mws ...Middleware) http.Handler {
	for i := len(mws) - 1; i >= 0; i-- {
		h = mws[i](h)
	}
	return h
}

type ctxKey string

const requestIDKey ctxKey = "request-id"

// RequestIDFrom returns the request ID stored by RequestID.
func RequestIDFrom(ctx context.Context) string {
	id, _ := ctx.Value(requestIDKey).(string)
	return id
}

// RequestID attaches a random ID to the context and X-Request-Id header.
func RequestID(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b := make([]byte, 8)
		_, _ = rand.Read(b)
		id := hex.EncodeToString(b)
		w.Header().Set("X-Request-Id", id)
		next.ServeHTTP(w, r.WithContext(context.WithValue(r.Context(), requestIDKey, id)))
	})
}

// statusRecorder captures the status code written by a handler.
type statusRecorder struct {
	http.ResponseWriter
	status int
}

func (s *statusRecorder) WriteHeader(code int) {
	s.status = code
	s.ResponseWriter.WriteHeader(code)
}

// Logging logs method, path, status and duration via logger.
func Logging(logger *log.Logger) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			start := time.Now()
			rec := &statusRecorder{ResponseWriter: w, status: http.StatusOK}
			next.ServeHTTP(rec, r)
			logger.Printf("id=%s %s %s -> %d (%s)", RequestIDFrom(r.Context()),
				r.Method, r.URL.Path, rec.status, time.Since(start).Round(time.Microsecond))
		})
	}
}

// Recover turns panics into 500 responses.
func Recover(logger *log.Logger) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			defer func() {
				if rec := recover(); rec != nil {
					logger.Printf("panic: %v", rec)
					http.Error(w, "internal server error", http.StatusInternalServerError)
				}
			}()
			next.ServeHTTP(w, r)
		})
	}
}

// bucket is a token bucket.
type bucket struct {
	tokens float64
	last   time.Time
}

// RateLimit allows `burst` requests per client IP, refilling at `perSec`.
// now is injectable for tests.
func RateLimit(perSec float64, burst int, now func() time.Time) Middleware {
	var mu sync.Mutex
	buckets := map[string]*bucket{}
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			ip, _, err := net.SplitHostPort(r.RemoteAddr)
			if err != nil {
				ip = r.RemoteAddr
			}
			t := now()
			mu.Lock()
			b, ok := buckets[ip]
			if !ok {
				b = &bucket{tokens: float64(burst), last: t}
				buckets[ip] = b
			}
			b.tokens += t.Sub(b.last).Seconds() * perSec
			if b.tokens > float64(burst) {
				b.tokens = float64(burst)
			}
			b.last = t
			allowed := b.tokens >= 1
			if allowed {
				b.tokens--
			}
			mu.Unlock()
			if !allowed {
				w.Header().Set("Retry-After", fmt.Sprint(int(1/perSec)+1))
				http.Error(w, "too many requests", http.StatusTooManyRequests)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}

// BearerAuth requires "Authorization: Bearer <token>" with a known token.
func BearerAuth(tokens map[string]string) Middleware {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			h := r.Header.Get("Authorization")
			tok, ok := strings.CutPrefix(h, "Bearer ")
			user, known := tokens[tok]
			if !ok || !known {
				w.Header().Set("WWW-Authenticate", `Bearer realm="api"`)
				http.Error(w, "unauthorized", http.StatusUnauthorized)
				return
			}
			next.ServeHTTP(w, r.WithContext(context.WithValue(r.Context(), ctxKey("user"), user)))
		})
	}
}

// UserFrom returns the authenticated user name.
func UserFrom(ctx context.Context) string {
	u, _ := ctx.Value(ctxKey("user")).(string)
	return u
}
