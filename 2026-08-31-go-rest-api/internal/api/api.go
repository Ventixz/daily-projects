// Package api wires the store to HTTP: routing, JSON encoding/decoding,
// status codes, and the middleware chain. Standard library only.
package api

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"time"

	"gorest/internal/store"
)

// NewServer builds the full handler: routes wrapped in logging and
// panic-recovery middleware.
func NewServer(s *store.Store) http.Handler {
	mux := http.NewServeMux()

	h := &handler{store: s}
	mux.HandleFunc("POST /books", h.create)
	mux.HandleFunc("GET /books", h.list)
	mux.HandleFunc("GET /books/{id}", h.get)
	mux.HandleFunc("PUT /books/{id}", h.update)
	mux.HandleFunc("DELETE /books/{id}", h.delete)

	return recoverMiddleware(loggingMiddleware(mux))
}

type handler struct {
	store *store.Store
}

type bookInput struct {
	Title  string `json:"title"`
	Author string `json:"author"`
	Year   int    `json:"year"`
}

func etag(version int) string {
	return fmt.Sprintf(`"v%d"`, version)
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if v != nil {
		_ = json.NewEncoder(w).Encode(v)
	}
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]string{"error": message})
}

func (h *handler) create(w http.ResponseWriter, r *http.Request) {
	var in bookInput
	if err := json.NewDecoder(r.Body).Decode(&in); err != nil {
		writeError(w, http.StatusBadRequest, "malformed JSON body")
		return
	}
	if in.Title == "" || in.Author == "" {
		writeError(w, http.StatusBadRequest, "title and author are required")
		return
	}

	b := h.store.Create(in.Title, in.Author, in.Year)
	w.Header().Set("Location", fmt.Sprintf("/books/%d", b.ID))
	w.Header().Set("ETag", etag(b.Version))
	writeJSON(w, http.StatusCreated, b)
}

func (h *handler) list(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()

	filter := store.ListFilter{Author: q.Get("author")}
	if v := q.Get("offset"); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil || n < 0 {
			writeError(w, http.StatusBadRequest, "offset must be a non-negative integer")
			return
		}
		filter.Offset = n
	}
	if v := q.Get("limit"); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil || n < 0 {
			writeError(w, http.StatusBadRequest, "limit must be a non-negative integer")
			return
		}
		filter.Limit = n
	}

	books, total := h.store.List(filter)
	w.Header().Set("X-Total-Count", strconv.Itoa(total))
	writeJSON(w, http.StatusOK, books)
}

func parseID(r *http.Request) (int, error) {
	return strconv.Atoi(r.PathValue("id"))
}

func (h *handler) get(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeError(w, http.StatusBadRequest, "id must be an integer")
		return
	}

	b, err := h.store.Get(id)
	if err == store.ErrNotFound {
		writeError(w, http.StatusNotFound, "book not found")
		return
	}
	w.Header().Set("ETag", etag(b.Version))
	writeJSON(w, http.StatusOK, b)
}

// update implements optimistic concurrency control: a client must read a
// book (and its ETag) before it can write one. Missing the If-Match header
// is a 428, a stale one is a 412 — RFC 6585's two dedicated status codes
// for exactly this situation, distinct from a plain 400.
func (h *handler) update(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeError(w, http.StatusBadRequest, "id must be an integer")
		return
	}

	current, err := h.store.Get(id)
	if err == store.ErrNotFound {
		writeError(w, http.StatusNotFound, "book not found")
		return
	}

	ifMatch := r.Header.Get("If-Match")
	if ifMatch == "" {
		writeError(w, http.StatusPreconditionRequired, "If-Match header is required")
		return
	}
	if ifMatch != etag(current.Version) {
		writeError(w, http.StatusPreconditionFailed, "resource has changed since it was last read")
		return
	}

	var in bookInput
	if err := json.NewDecoder(r.Body).Decode(&in); err != nil {
		writeError(w, http.StatusBadRequest, "malformed JSON body")
		return
	}
	if in.Title == "" || in.Author == "" {
		writeError(w, http.StatusBadRequest, "title and author are required")
		return
	}

	updated, err := h.store.Update(id, current.Version, in.Title, in.Author, in.Year)
	switch err {
	case nil:
		w.Header().Set("ETag", etag(updated.Version))
		writeJSON(w, http.StatusOK, updated)
	case store.ErrVersionMismatch:
		// Someone else updated it between our Get and our Update above.
		writeError(w, http.StatusPreconditionFailed, "resource has changed since it was last read")
	case store.ErrNotFound:
		writeError(w, http.StatusNotFound, "book not found")
	}
}

func (h *handler) delete(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeError(w, http.StatusBadRequest, "id must be an integer")
		return
	}

	if err := h.store.Delete(id); err == store.ErrNotFound {
		writeError(w, http.StatusNotFound, "book not found")
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		sw := &statusWriter{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(sw, r)
		log.Printf("%s %s -> %d (%s)", r.Method, r.URL.Path, sw.status, time.Since(start))
	})
}

// recoverMiddleware turns a panic anywhere in the handler chain into a JSON
// 500 instead of taking the whole process down.
func recoverMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if rec := recover(); rec != nil {
				log.Printf("recovered panic on %s %s: %v", r.Method, r.URL.Path, rec)
				writeError(w, http.StatusInternalServerError, "internal server error")
			}
		}()
		next.ServeHTTP(w, r)
	})
}

type statusWriter struct {
	http.ResponseWriter
	status int
}

func (sw *statusWriter) WriteHeader(status int) {
	sw.status = status
	sw.ResponseWriter.WriteHeader(status)
}
