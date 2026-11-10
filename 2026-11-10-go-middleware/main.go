package main

import (
	"fmt"
	"log"
	"net/http"
	"os"
)

// NewHandler wires a small API: /health is public, /secret needs a token, /boom panics.
func NewHandler(logger *log.Logger, token string, limiter *RateLimiter) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "ok %s\n", RequestIDFrom(r.Context()))
	})
	mux.Handle("GET /secret", BearerAuth(token)(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "the answer is 42")
	})))
	mux.HandleFunc("GET /boom", func(http.ResponseWriter, *http.Request) { panic("kaboom") })
	return Chain(mux, RequestID, Logging(logger), Recover(logger), limiter.Middleware())
}

func main() {
	token := os.Getenv("API_TOKEN")
	if token == "" {
		token = "dev-token"
	}
	logger := log.New(os.Stdout, "", log.LstdFlags)
	h := NewHandler(logger, token, NewRateLimiter(5, 10, nil))
	logger.Println("listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", h))
}
