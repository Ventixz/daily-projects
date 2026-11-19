package taskstore

import (
	"testing"
	"time"
)

func TestOwnershipIsolation(t *testing.T) {
	s := New()
	due := time.Date(2026, 11, 20, 9, 0, 0, 0, time.UTC)
	a := s.Create("alice", "buy milk", []string{"home"}, due)
	b := s.Create("bob", "fix bike", []string{"home"}, due)

	if _, err := s.Get("alice", a); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Get("alice", b); err != ErrNotFound {
		t.Fatalf("alice read bob's task: err=%v", err)
	}
	if err := s.Delete("alice", b); err != ErrNotFound {
		t.Fatalf("alice deleted bob's task: err=%v", err)
	}
	if got := s.All("alice"); len(got) != 1 || got[0].ID != a {
		t.Fatalf("All(alice) = %+v", got)
	}
	if got := s.ByTag("bob", "home"); len(got) != 1 || got[0].ID != b {
		t.Fatalf("ByTag(bob) = %+v", got)
	}
	if n := s.DeleteAll("alice"); n != 1 {
		t.Fatalf("DeleteAll(alice) = %d", n)
	}
	if _, err := s.Get("bob", b); err != nil {
		t.Fatal("DeleteAll(alice) removed bob's task")
	}
}

func TestFiltersAndCopies(t *testing.T) {
	s := New()
	tags := []string{"work"}
	s.Create("a", "one", tags, time.Date(2026, 1, 2, 0, 0, 0, 0, time.UTC))
	s.Create("a", "two", nil, time.Date(2026, 1, 3, 0, 0, 0, 0, time.UTC))
	tags[0] = "mutated"
	if got := s.ByTag("a", "work"); len(got) != 1 {
		t.Fatalf("store kept caller's slice: %+v", got)
	}
	if got := s.ByDue("a", 2026, time.January, 3); len(got) != 1 || got[0].Text != "two" {
		t.Fatalf("ByDue = %+v", got)
	}
	if got := s.All("a"); got[1].Tags == nil {
		t.Fatal("nil tags should be stored as []")
	}
	if got := s.All("nobody"); got == nil || len(got) != 0 {
		t.Fatal("empty result should be a non-nil empty slice (JSON [])")
	}
}
