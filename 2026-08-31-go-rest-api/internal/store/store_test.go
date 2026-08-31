package store

import (
	"sync"
	"testing"
)

func TestCreateAssignsIncrementingIDs(t *testing.T) {
	s := New()
	a := s.Create("Dune", "Frank Herbert", 1965)
	b := s.Create("Foundation", "Isaac Asimov", 1951)

	if a.ID != 1 || b.ID != 2 {
		t.Fatalf("want ids 1, 2; got %d, %d", a.ID, b.ID)
	}
	if a.Version != 1 || b.Version != 1 {
		t.Fatalf("want both books at version 1; got %d, %d", a.Version, b.Version)
	}
}

func TestGetNotFound(t *testing.T) {
	s := New()
	if _, err := s.Get(99); err != ErrNotFound {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestUpdateRequiresMatchingVersion(t *testing.T) {
	s := New()
	b := s.Create("Dune", "Frank Herbert", 1965)

	if _, err := s.Update(b.ID, b.Version+1, "Dune", "F. Herbert", 1965); err != ErrVersionMismatch {
		t.Fatalf("want ErrVersionMismatch, got %v", err)
	}

	updated, err := s.Update(b.ID, b.Version, "Dune", "F. Herbert", 1965)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if updated.Author != "F. Herbert" || updated.Version != 2 {
		t.Fatalf("want updated author and version 2, got %+v", updated)
	}

	// The version that just succeeded is now stale.
	if _, err := s.Update(b.ID, b.Version, "Dune", "Someone Else", 1965); err != ErrVersionMismatch {
		t.Fatalf("want stale version to be rejected, got %v", err)
	}
}

func TestUpdateNotFound(t *testing.T) {
	s := New()
	if _, err := s.Update(42, 1, "x", "y", 2000); err != ErrNotFound {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestDelete(t *testing.T) {
	s := New()
	b := s.Create("Dune", "Frank Herbert", 1965)

	if err := s.Delete(b.ID); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if _, err := s.Get(b.ID); err != ErrNotFound {
		t.Fatalf("want book gone after delete, got %v", err)
	}
	if err := s.Delete(b.ID); err != ErrNotFound {
		t.Fatalf("want deleting twice to 404, got %v", err)
	}
}

func TestListFilterAndPagination(t *testing.T) {
	s := New()
	s.Create("Dune", "Frank Herbert", 1965)
	s.Create("Children of Dune", "Frank Herbert", 1976)
	s.Create("Foundation", "Isaac Asimov", 1951)

	all, total := s.List(ListFilter{})
	if total != 3 || len(all) != 3 {
		t.Fatalf("want 3 books total, got %d (%d listed)", total, len(all))
	}

	herbert, total := s.List(ListFilter{Author: "herbert"})
	if total != 2 || len(herbert) != 2 {
		t.Fatalf("want 2 Herbert books, got %d (%d listed)", total, len(herbert))
	}

	page, total := s.List(ListFilter{Offset: 1, Limit: 1})
	if total != 3 {
		t.Fatalf("want total to reflect the unpaginated match count, got %d", total)
	}
	if len(page) != 1 || page[0].Title != "Children of Dune" {
		t.Fatalf("want page 2 (offset 1, limit 1) to be 'Children of Dune', got %+v", page)
	}

	beyond, total := s.List(ListFilter{Offset: 99})
	if len(beyond) != 0 || total != 3 {
		t.Fatalf("want an out-of-range offset to return an empty page with the real total, got %d items, total %d", len(beyond), total)
	}
}

// TestConcurrentAccess exercises the store from many goroutines at once.
// Run with `go test -race` — the point isn't the assertion below so much as
// proving the mutex actually serializes the map access.
func TestConcurrentAccess(t *testing.T) {
	s := New()
	const workers = 50

	var wg sync.WaitGroup
	wg.Add(workers)
	for i := 0; i < workers; i++ {
		go func(n int) {
			defer wg.Done()
			b := s.Create("Book", "Author", 2000+n)
			for {
				got, err := s.Get(b.ID)
				if err != nil {
					t.Errorf("Get(%d): %v", b.ID, err)
					return
				}
				if _, err := s.Update(b.ID, got.Version, "Book Updated", "Author", got.Year); err == nil {
					break
				}
			}
		}(i)
	}
	wg.Wait()

	_, total := s.List(ListFilter{})
	if total != workers {
		t.Fatalf("want %d books after concurrent creates, got %d", workers, total)
	}
}
