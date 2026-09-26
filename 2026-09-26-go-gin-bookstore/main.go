package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"bookstore/internal/api"
	"bookstore/internal/store"
)

func main() {
	addr := envOr("BOOKSTORE_ADDR", ":8080")
	apiKey := envOr("BOOKSTORE_API_KEY", "dev-key")

	router := api.NewRouter(store.New(), apiKey)
	srv := &http.Server{Addr: addr, Handler: router}

	go func() {
		log.Printf("bookstore listening on %s", addr)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatalf("listen: %v", err)
		}
	}()

	// Block until the process is asked to stop, then give in-flight
	// requests a grace period instead of dropping them mid-response.
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	<-stop

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := srv.Shutdown(ctx); err != nil {
		log.Fatalf("graceful shutdown failed: %v", err)
	}
	log.Println("bookstore stopped")
}

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}
