package store

import (
	"errors"
	"testing"
)

func TestCreateAssignsSequentialIDs(t *testing.T) {
	s := New()

	a, err := s.Create(Book{ISBN: "111", Title: "A"})
	if err != nil {
		t.Fatalf("create a: %v", err)
	}
	b, err := s.Create(Book{ISBN: "222", Title: "B"})
	if err != nil {
		t.Fatalf("create b: %v", err)
	}

	if a.ID != 1 || b.ID != 2 {
		t.Fatalf("want IDs 1,2, got %d,%d", a.ID, b.ID)
	}
}

func TestCreateRejectsDuplicateISBN(t *testing.T) {
	s := New()
	if _, err := s.Create(Book{ISBN: "111", Title: "A"}); err != nil {
		t.Fatalf("first create: %v", err)
	}

	_, err := s.Create(Book{ISBN: "111", Title: "A again"})
	if !errors.Is(err, ErrDuplicateISBN) {
		t.Fatalf("want ErrDuplicateISBN, got %v", err)
	}
}

func TestGetMissingReturnsNotFound(t *testing.T) {
	s := New()
	if _, err := s.Get(99); !errors.Is(err, ErrNotFound) {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestListIsSortedByIDRegardlessOfInsertOrDeleteOrder(t *testing.T) {
	s := New()
	first, _ := s.Create(Book{ISBN: "1"})
	second, _ := s.Create(Book{ISBN: "2"})
	third, _ := s.Create(Book{ISBN: "3"})

	if err := s.Delete(second.ID); err != nil {
		t.Fatalf("delete: %v", err)
	}

	got := s.List()
	if len(got) != 2 || got[0].ID != first.ID || got[1].ID != third.ID {
		t.Fatalf("want [%d,%d], got %+v", first.ID, third.ID, got)
	}
}

func TestUpdateChangingISBNMovesTheIndex(t *testing.T) {
	s := New()
	created, _ := s.Create(Book{ISBN: "old", Title: "T"})

	updated, err := s.Update(created.ID, Book{ISBN: "new", Title: "T2"})
	if err != nil {
		t.Fatalf("update: %v", err)
	}
	if updated.ISBN != "new" {
		t.Fatalf("want isbn 'new', got %q", updated.ISBN)
	}

	// The old ISBN must be free again for a new book to claim.
	if _, err := s.Create(Book{ISBN: "old", Title: "reuse"}); err != nil {
		t.Fatalf("expected old ISBN to be reusable, got %v", err)
	}
}

func TestUpdateRejectsISBNOwnedByAnotherBook(t *testing.T) {
	s := New()
	a, _ := s.Create(Book{ISBN: "a-isbn"})
	b, _ := s.Create(Book{ISBN: "b-isbn"})

	_, err := s.Update(b.ID, Book{ISBN: a.ISBN})
	if !errors.Is(err, ErrDuplicateISBN) {
		t.Fatalf("want ErrDuplicateISBN, got %v", err)
	}
}

func TestUpdateMissingReturnsNotFound(t *testing.T) {
	s := New()
	if _, err := s.Update(42, Book{ISBN: "x"}); !errors.Is(err, ErrNotFound) {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestDeleteFreesISBNForReuse(t *testing.T) {
	s := New()
	created, _ := s.Create(Book{ISBN: "111"})

	if err := s.Delete(created.ID); err != nil {
		t.Fatalf("delete: %v", err)
	}
	if _, err := s.Create(Book{ISBN: "111"}); err != nil {
		t.Fatalf("expected ISBN reuse after delete, got %v", err)
	}
}

func TestDeleteMissingReturnsNotFound(t *testing.T) {
	s := New()
	if err := s.Delete(1); !errors.Is(err, ErrNotFound) {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}
