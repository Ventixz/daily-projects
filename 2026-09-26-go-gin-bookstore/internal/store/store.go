// Package store holds the bookstore's domain model and in-memory persistence.
package store

import (
	"errors"
	"sync"
)

var (
	ErrNotFound     = errors.New("book not found")
	ErrDuplicateISBN = errors.New("a book with this ISBN already exists")
)

// Book is the domain entity. JSON tags are shared with the API layer since
// this project has no separate DTO layer.
type Book struct {
	ID       int     `json:"id"`
	ISBN     string  `json:"isbn"`
	Title    string  `json:"title"`
	Author   string  `json:"author"`
	Price    float64 `json:"price"`
	Quantity int     `json:"quantity"`
}

// Store is a thread-safe in-memory book repository, keyed by ID with a
// secondary ISBN index to reject duplicates in O(1).
type Store struct {
	mu     sync.RWMutex
	books  map[int]Book
	isbns  map[string]int // isbn -> id
	nextID int
}

func New() *Store {
	return &Store{
		books:  make(map[int]Book),
		isbns:  make(map[string]int),
		nextID: 1,
	}
}

// Create assigns an ID and stores b, rejecting a duplicate ISBN before it
// ever reaches the map so List/Get never observe a half-inserted book.
func (s *Store) Create(b Book) (Book, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.isbns[b.ISBN]; exists {
		return Book{}, ErrDuplicateISBN
	}

	b.ID = s.nextID
	s.nextID++
	s.books[b.ID] = b
	s.isbns[b.ISBN] = b.ID
	return b, nil
}

func (s *Store) Get(id int) (Book, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	b, ok := s.books[id]
	if !ok {
		return Book{}, ErrNotFound
	}
	return b, nil
}

// List returns books sorted by ID so pagination is stable across calls even
// though Go map iteration order isn't.
func (s *Store) List() []Book {
	s.mu.RLock()
	defer s.mu.RUnlock()

	out := make([]Book, 0, len(s.books))
	for id := 1; id < s.nextID; id++ {
		if b, ok := s.books[id]; ok {
			out = append(out, b)
		}
	}
	return out
}

// Update replaces the book at id in place, keeping the ISBN index consistent
// when the ISBN itself changes as part of the update.
func (s *Store) Update(id int, b Book) (Book, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	existing, ok := s.books[id]
	if !ok {
		return Book{}, ErrNotFound
	}

	if b.ISBN != existing.ISBN {
		if ownerID, exists := s.isbns[b.ISBN]; exists && ownerID != id {
			return Book{}, ErrDuplicateISBN
		}
		delete(s.isbns, existing.ISBN)
		s.isbns[b.ISBN] = id
	}

	b.ID = id
	s.books[id] = b
	return b, nil
}

func (s *Store) Delete(id int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	b, ok := s.books[id]
	if !ok {
		return ErrNotFound
	}
	delete(s.books, id)
	delete(s.isbns, b.ISBN)
	return nil
}
