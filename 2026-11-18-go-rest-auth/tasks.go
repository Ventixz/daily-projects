package main

import (
	"encoding/json"
	"errors"
	"mime"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Task is the resource from the "REST Servers in Go" series, plus an Owner
// recorded from the authenticated user.
type Task struct {
	ID    int       `json:"id"`
	Text  string    `json:"text"`
	Tags  []string  `json:"tags"`
	Due   time.Time `json:"due"`
	Owner string    `json:"owner"`
}

// TaskStore is a mutex-guarded in-memory store.
type TaskStore struct {
	mu     sync.Mutex
	tasks  map[int]Task
	nextID int
}

func NewTaskStore() *TaskStore { return &TaskStore{tasks: map[int]Task{}} }

func (s *TaskStore) Create(t Task) Task {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.nextID++
	t.ID = s.nextID
	if t.Tags == nil {
		t.Tags = []string{}
	}
	s.tasks[t.ID] = t
	return t
}

func (s *TaskStore) Get(id int) (Task, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tasks[id]
	return t, ok
}

func (s *TaskStore) All() []Task {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]Task, 0, len(s.tasks))
	for _, t := range s.tasks {
		out = append(out, t)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

var (
	errNotFound  = errors.New("not found")
	errForbidden = errors.New("forbidden")
)

// DeleteAs removes a task only if user owns it. Authentication says who you
// are; this check (authorization) says what you may do.
func (s *TaskStore) DeleteAs(id int, user string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tasks[id]
	if !ok {
		return errNotFound
	}
	if t.Owner != user {
		return errForbidden
	}
	delete(s.tasks, id)
	return nil
}

// NewAPI wires the routes. Reads are public; writes go through BasicAuth.
func NewAPI(store *TaskStore, users *UserStore) http.Handler {
	mux := http.NewServeMux()
	protect := func(h http.HandlerFunc) http.Handler { return BasicAuth(users, "tasks", h) }

	mux.HandleFunc("GET /task/", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, store.All())
	})
	mux.HandleFunc("GET /task/{id}", func(w http.ResponseWriter, r *http.Request) {
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			http.Error(w, "bad id", http.StatusBadRequest)
			return
		}
		t, ok := store.Get(id)
		if !ok {
			http.Error(w, "not found", http.StatusNotFound)
			return
		}
		writeJSON(w, http.StatusOK, t)
	})
	mux.Handle("POST /task/", protect(func(w http.ResponseWriter, r *http.Request) {
		if mt, _, err := mime.ParseMediaType(r.Header.Get("Content-Type")); err != nil || mt != "application/json" {
			http.Error(w, "expect application/json", http.StatusUnsupportedMediaType)
			return
		}
		var in struct {
			Text string    `json:"text"`
			Tags []string  `json:"tags"`
			Due  time.Time `json:"due"`
		}
		dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20))
		dec.DisallowUnknownFields()
		if err := dec.Decode(&in); err != nil {
			http.Error(w, "bad json: "+err.Error(), http.StatusBadRequest)
			return
		}
		if strings.TrimSpace(in.Text) == "" {
			http.Error(w, "text is required", http.StatusBadRequest)
			return
		}
		user, _ := UserFromContext(r.Context())
		t := store.Create(Task{Text: in.Text, Tags: in.Tags, Due: in.Due, Owner: user})
		w.Header().Set("Location", "/task/"+strconv.Itoa(t.ID))
		writeJSON(w, http.StatusCreated, t)
	}))
	mux.Handle("DELETE /task/{id}", protect(func(w http.ResponseWriter, r *http.Request) {
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			http.Error(w, "bad id", http.StatusBadRequest)
			return
		}
		user, _ := UserFromContext(r.Context())
		switch err := store.DeleteAs(id, user); err {
		case nil:
			w.WriteHeader(http.StatusNoContent)
		case errNotFound:
			http.Error(w, "not found", http.StatusNotFound)
		default:
			http.Error(w, "not your task", http.StatusForbidden)
		}
	}))
	mux.Handle("GET /whoami", protect(func(w http.ResponseWriter, r *http.Request) {
		user, _ := UserFromContext(r.Context())
		writeJSON(w, http.StatusOK, map[string]string{"user": user})
	}))
	return mux
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}
