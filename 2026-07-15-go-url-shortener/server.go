package main

import (
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"strings"

	"url-shortener/internal/shortener"
)

// newServer wires up routes against store and returns a handler. Kept
// separate from main() so tests can exercise the whole HTTP surface with
// httptest instead of a real listener.
func newServer(store *shortener.Store, staticDir string) http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("GET /{$}", handleIndex(staticDir))
	mux.Handle("GET /static/", http.StripPrefix("/static/", http.FileServer(http.Dir(staticDir))))
	mux.HandleFunc("POST /api/shorten", handleShorten(store))
	mux.HandleFunc("GET /api/links", handleList(store))
	mux.HandleFunc("GET /api/stats/{code}", handleStats(store))
	mux.HandleFunc("GET /{code}", handleRedirect(store))

	return logRequests(mux)
}

func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		next.ServeHTTP(w, r)
		log.Printf("%s %s", r.Method, r.URL.Path)
	})
}

func handleIndex(staticDir string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, staticDir+"/index.html")
	}
}

type shortenRequest struct {
	URL    string `json:"url"`
	Custom string `json:"custom,omitempty"`
}

type shortenResponse struct {
	Code     string `json:"code"`
	URL      string `json:"url"`
	ShortURL string `json:"short_url"`
}

func handleShorten(store *shortener.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req shortenRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "malformed JSON body")
			return
		}

		link, err := store.Create(req.URL, req.Custom)
		if err != nil {
			status := http.StatusBadRequest
			if errors.Is(err, shortener.ErrCodeTaken) {
				status = http.StatusConflict
			}
			writeError(w, status, err.Error())
			return
		}

		writeJSON(w, http.StatusCreated, shortenResponse{
			Code:     link.Code,
			URL:      link.URL,
			ShortURL: baseURL(r) + "/" + link.Code,
		})
	}
}

func handleRedirect(store *shortener.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		code := r.PathValue("code")
		link, ok := store.Get(code)
		if !ok {
			writeError(w, http.StatusNotFound, "no link with that code")
			return
		}
		_ = store.RecordClick(code)
		http.Redirect(w, r, link.URL, http.StatusFound)
	}
}

func handleStats(store *shortener.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		code := r.PathValue("code")
		link, ok := store.Get(code)
		if !ok {
			writeError(w, http.StatusNotFound, "no link with that code")
			return
		}
		writeJSON(w, http.StatusOK, link)
	}
}

func handleList(store *shortener.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, store.All())
	}
}

func baseURL(r *http.Request) string {
	scheme := "http"
	if r.TLS != nil || r.Header.Get("X-Forwarded-Proto") == "https" {
		scheme = "https"
	}
	return scheme + "://" + r.Host
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": strings.TrimSpace(msg)})
}
