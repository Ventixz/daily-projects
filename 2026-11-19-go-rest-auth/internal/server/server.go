// Package server is the task REST API: Go 1.22 pattern routing, JSON in and
// out, behind TLS and Basic auth.
package server

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

	"restauth/internal/auth"
	"restauth/internal/taskstore"
)

const maxBody = 1 << 16 // 64 KiB is plenty for a task

type Server struct {
	store *taskstore.Store
}

// New builds the full handler: public routes, then the authenticated API.
//
//	GET    /healthz                    (no auth)
//	GET    /whoami
//	POST   /task/                      {"text":..., "tags":[...], "due":RFC3339}
//	GET    /task/
//	DELETE /task/
//	GET    /task/{id}/
//	DELETE /task/{id}/
//	GET    /tag/{tag}/
//	GET    /due/{year}/{month}/{day}/
func New(store *taskstore.Store, users auth.Authenticator, realm string) http.Handler {
	s := &Server{store: store}

	api := http.NewServeMux()
	api.HandleFunc("GET /whoami", s.whoami)
	api.HandleFunc("POST /task/", s.createTask)
	api.HandleFunc("GET /task/", s.listTasks)
	api.HandleFunc("DELETE /task/", s.deleteAll)
	api.HandleFunc("GET /task/{id}/", s.getTask)
	api.HandleFunc("DELETE /task/{id}/", s.deleteTask)
	api.HandleFunc("GET /tag/{tag}/", s.byTag)
	api.HandleFunc("GET /due/{year}/{month}/{day}/", s.byDue)

	root := http.NewServeMux()
	root.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "ok")
	})
	root.Handle("/", auth.BasicAuth(users, realm, noStore(api)))
	return securityHeaders(auth.RequireTLS(root))
}

// securityHeaders sets headers every response should carry. HSTS tells the
// browser to use https:// for this host from now on, even if the user types
// http://, so the plain-HTTP redirect is only ever hit once.
func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := w.Header()
		h.Set("Strict-Transport-Security", "max-age=63072000; includeSubDomains")
		h.Set("X-Content-Type-Options", "nosniff")
		next.ServeHTTP(w, r)
	})
}

// noStore keeps per-user responses out of shared caches.
func noStore(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		next.ServeHTTP(w, r)
	})
}

// RedirectToHTTPS answers plain-HTTP requests with a permanent redirect to the
// same path on the HTTPS port. It never reads the body or credentials.
func RedirectToHTTPS(httpsPort string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		host := r.Host
		if h, _, err := net.SplitHostPort(host); err == nil {
			host = h
		}
		if strings.ContainsAny(host, "/\\@") || host == "" {
			http.Error(w, "bad host", http.StatusBadRequest)
			return
		}
		target := "https://" + host
		if httpsPort != "443" {
			target += ":" + httpsPort
		}
		target += r.URL.RequestURI()
		http.Redirect(w, r, target, http.StatusPermanentRedirect)
	})
}

func user(r *http.Request) string {
	name, _ := auth.UserFromContext(r.Context())
	return name
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": msg})
}

func (s *Server) whoami(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"user": user(r)})
}

type createRequest struct {
	Text string    `json:"text"`
	Tags []string  `json:"tags"`
	Due  time.Time `json:"due"`
}

func (s *Server) createTask(w http.ResponseWriter, r *http.Request) {
	if ct := r.Header.Get("Content-Type"); !strings.HasPrefix(ct, "application/json") {
		writeError(w, http.StatusUnsupportedMediaType, "expect application/json")
		return
	}
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxBody))
	dec.DisallowUnknownFields()
	var req createRequest
	if err := dec.Decode(&req); err != nil {
		var tooBig *http.MaxBytesError
		if errors.As(err, &tooBig) {
			writeError(w, http.StatusRequestEntityTooLarge, "body too large")
			return
		}
		writeError(w, http.StatusBadRequest, "invalid JSON: "+err.Error())
		return
	}
	if strings.TrimSpace(req.Text) == "" {
		writeError(w, http.StatusBadRequest, "text is required")
		return
	}
	id := s.store.Create(user(r), req.Text, req.Tags, req.Due)
	writeJSON(w, http.StatusCreated, map[string]int{"id": id})
}

func (s *Server) listTasks(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, s.store.All(user(r)))
}

func (s *Server) deleteAll(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]int{"deleted": s.store.DeleteAll(user(r))})
}

func pathID(w http.ResponseWriter, r *http.Request) (int, bool) {
	id, err := strconv.Atoi(r.PathValue("id"))
	if err != nil || id < 0 {
		writeError(w, http.StatusBadRequest, "bad id")
		return 0, false
	}
	return id, true
}

func (s *Server) getTask(w http.ResponseWriter, r *http.Request) {
	id, ok := pathID(w, r)
	if !ok {
		return
	}
	t, err := s.store.Get(user(r), id)
	if err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, t)
}

func (s *Server) deleteTask(w http.ResponseWriter, r *http.Request) {
	id, ok := pathID(w, r)
	if !ok {
		return
	}
	if err := s.store.Delete(user(r), id); err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) byTag(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, s.store.ByTag(user(r), r.PathValue("tag")))
}

func (s *Server) byDue(w http.ResponseWriter, r *http.Request) {
	y, e1 := strconv.Atoi(r.PathValue("year"))
	m, e2 := strconv.Atoi(r.PathValue("month"))
	d, e3 := strconv.Atoi(r.PathValue("day"))
	if e1 != nil || e2 != nil || e3 != nil || m < 1 || m > 12 || d < 1 || d > 31 {
		writeError(w, http.StatusBadRequest, "want /due/<year>/<month>/<day>/")
		return
	}
	writeJSON(w, http.StatusOK, s.store.ByDue(user(r), y, time.Month(m), d))
}
