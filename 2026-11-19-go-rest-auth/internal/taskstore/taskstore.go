// Package taskstore is the in-memory task store used throughout the "REST
// Servers in Go" series, extended so every task has an owner. Every method
// takes the owner, and a task is invisible to anyone else.
package taskstore

import (
	"errors"
	"slices"
	"sync"
	"time"
)

// ErrNotFound is returned for missing tasks and for tasks owned by someone
// else. Using one error for both means the API can't be used to probe which
// IDs exist.
var ErrNotFound = errors.New("task not found")

type Task struct {
	ID    int       `json:"id"`
	Owner string    `json:"owner"`
	Text  string    `json:"text"`
	Tags  []string  `json:"tags"`
	Due   time.Time `json:"due"`
}

type Store struct {
	mu     sync.Mutex
	tasks  map[int]Task
	nextID int
}

func New() *Store { return &Store{tasks: map[int]Task{}} }

func (s *Store) Create(owner, text string, tags []string, due time.Time) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	t := Task{ID: s.nextID, Owner: owner, Text: text, Tags: slices.Clone(tags), Due: due}
	if t.Tags == nil {
		t.Tags = []string{}
	}
	s.tasks[t.ID] = t
	s.nextID++
	return t.ID
}

func (s *Store) Get(owner string, id int) (Task, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tasks[id]
	if !ok || t.Owner != owner {
		return Task{}, ErrNotFound
	}
	return t, nil
}

func (s *Store) Delete(owner string, id int) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tasks[id]
	if !ok || t.Owner != owner {
		return ErrNotFound
	}
	delete(s.tasks, id)
	return nil
}

func (s *Store) DeleteAll(owner string) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	n := 0
	for id, t := range s.tasks {
		if t.Owner == owner {
			delete(s.tasks, id)
			n++
		}
	}
	return n
}

func (s *Store) filter(owner string, keep func(Task) bool) []Task {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := []Task{}
	for _, t := range s.tasks {
		if t.Owner == owner && keep(t) {
			out = append(out, t)
		}
	}
	slices.SortFunc(out, func(a, b Task) int { return a.ID - b.ID })
	return out
}

func (s *Store) All(owner string) []Task {
	return s.filter(owner, func(Task) bool { return true })
}

func (s *Store) ByTag(owner, tag string) []Task {
	return s.filter(owner, func(t Task) bool { return slices.Contains(t.Tags, tag) })
}

func (s *Store) ByDue(owner string, year int, month time.Month, day int) []Task {
	return s.filter(owner, func(t Task) bool {
		y, m, d := t.Due.Date()
		return y == year && m == month && d == day
	})
}
