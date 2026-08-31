// Package store holds the in-memory, concurrency-safe book collection.
package store

import (
	"errors"
	"strings"
	"sync"
)

// ErrNotFound is returned when a book id doesn't exist in the store.
var ErrNotFound = errors.New("book not found")

// Book is the resource the API exposes. Version increments on every
// successful update and backs the HTTP ETag used for optimistic
// concurrency control.
type Book struct {
	ID      int    `json:"id"`
	Title   string `json:"title"`
	Author  string `json:"author"`
	Year    int    `json:"year"`
	Version int    `json:"version"`
}

// Store is a thread-safe in-memory collection of books.
type Store struct {
	mu     sync.RWMutex
	books  map[int]Book
	nextID int
}

// New returns an empty Store.
func New() *Store {
	return &Store{
		books:  make(map[int]Book),
		nextID: 1,
	}
}

// Create inserts a new book and assigns it an id and version 1.
func (s *Store) Create(title, author string, year int) Book {
	s.mu.Lock()
	defer s.mu.Unlock()

	b := Book{
		ID:      s.nextID,
		Title:   title,
		Author:  author,
		Year:    year,
		Version: 1,
	}
	s.books[b.ID] = b
	s.nextID++
	return b
}

// Get returns the book with the given id.
func (s *Store) Get(id int) (Book, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	b, ok := s.books[id]
	if !ok {
		return Book{}, ErrNotFound
	}
	return b, nil
}

// ListFilter narrows and paginates List.
type ListFilter struct {
	Author string // case-insensitive substring match against Book.Author
	Offset int
	Limit  int // 0 means "no limit"
}

// List returns books matching filter, ordered by id, along with the total
// count that matched before pagination was applied.
func (s *Store) List(filter ListFilter) ([]Book, int) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	matched := make([]Book, 0, len(s.books))
	for id := 1; id < s.nextID; id++ {
		b, ok := s.books[id]
		if !ok {
			continue
		}
		if filter.Author != "" && !strings.Contains(strings.ToLower(b.Author), strings.ToLower(filter.Author)) {
			continue
		}
		matched = append(matched, b)
	}

	total := len(matched)

	if filter.Offset >= len(matched) {
		return []Book{}, total
	}
	matched = matched[filter.Offset:]

	if filter.Limit > 0 && filter.Limit < len(matched) {
		matched = matched[:filter.Limit]
	}
	return matched, total
}

// Update replaces title/author/year on the book with the given id, but only
// if expectedVersion matches its current version. It returns the updated
// book (with version incremented) or ErrVersionMismatch/ErrNotFound.
var ErrVersionMismatch = errors.New("version mismatch")

func (s *Store) Update(id int, expectedVersion int, title, author string, year int) (Book, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	b, ok := s.books[id]
	if !ok {
		return Book{}, ErrNotFound
	}
	if b.Version != expectedVersion {
		return Book{}, ErrVersionMismatch
	}

	b.Title = title
	b.Author = author
	b.Year = year
	b.Version++
	s.books[id] = b
	return b, nil
}

// Delete removes the book with the given id.
func (s *Store) Delete(id int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, ok := s.books[id]; !ok {
		return ErrNotFound
	}
	delete(s.books, id)
	return nil
}
