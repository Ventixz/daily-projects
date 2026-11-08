package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"time"
)

// NewHandler builds the app: /health is public, /whoami needs auth,
// /boom panics (to show recovery).
func NewHandler(logger *log.Logger, now func() time.Time) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("ok"))
	})
	auth := BearerAuth(map[string]string{"s3cret": "alice"})
	mux.Handle("/whoami", auth(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"user": UserFrom(r.Context())})
	})))
	mux.HandleFunc("/boom", func(w http.ResponseWriter, r *http.Request) { panic("kaboom") })
	return Chain(mux, RequestID, Logging(logger), Recover(logger), RateLimit(5, 10, now))
}

func main() {
	logger := log.New(os.Stdout, "", log.LstdFlags)
	addr := ":8080"
	logger.Println("listening on", addr, "(try: curl -H 'Authorization: Bearer s3cret' localhost:8080/whoami)")
	log.Fatal(http.ListenAndServe(addr, NewHandler(logger, time.Now)))
}
