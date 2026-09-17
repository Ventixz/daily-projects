package library

import (
	"errors"
	"testing"
	"time"
)

var epoch = time.Date(2026, 1, 1, 9, 0, 0, 0, time.UTC)

func newTestLibrary() *Library {
	l := New()
	l.AddBook(Book{ISBN: "978-1", Title: "Clean Code", TotalCopies: 1, AvailableCopies: 1})
	l.AddMember(Member{ID: "alice", Name: "Alice", BorrowLimit: 2})
	return l
}

func TestBorrowDecrementsAvailableCopies(t *testing.T) {
	l := newTestLibrary()

	if _, err := l.Borrow("alice", "978-1", epoch); err != nil {
		t.Fatalf("Borrow returned unexpected error: %v", err)
	}

	book, _ := l.Book("978-1")
	if book.AvailableCopies != 0 {
		t.Errorf("AvailableCopies = %d, want 0", book.AvailableCopies)
	}
	if got := l.ActiveLoanCount("alice"); got != 1 {
		t.Errorf("ActiveLoanCount = %d, want 1", got)
	}
}

func TestBorrowFailsWhenNoCopiesAvailable(t *testing.T) {
	l := newTestLibrary()
	l.AddMember(Member{ID: "bob", Name: "Bob", BorrowLimit: 2})

	if _, err := l.Borrow("alice", "978-1", epoch); err != nil {
		t.Fatalf("first Borrow returned unexpected error: %v", err)
	}

	_, err := l.Borrow("bob", "978-1", epoch)
	if !errors.Is(err, ErrNoCopiesAvailable) {
		t.Errorf("second Borrow error = %v, want ErrNoCopiesAvailable", err)
	}
}

func TestBorrowFailsAtMembersLimit(t *testing.T) {
	l := newTestLibrary()
	l.AddBook(Book{ISBN: "978-2", Title: "The Hobbit", TotalCopies: 1, AvailableCopies: 1})
	l.AddBook(Book{ISBN: "978-3", Title: "Dune", TotalCopies: 1, AvailableCopies: 1})

	if _, err := l.Borrow("alice", "978-1", epoch); err != nil {
		t.Fatalf("Borrow 1 returned unexpected error: %v", err)
	}
	if _, err := l.Borrow("alice", "978-2", epoch); err != nil {
		t.Fatalf("Borrow 2 returned unexpected error: %v", err)
	}

	_, err := l.Borrow("alice", "978-3", epoch)
	if !errors.Is(err, ErrBorrowLimitReached) {
		t.Errorf("Borrow 3 error = %v, want ErrBorrowLimitReached", err)
	}
}

func TestBorrowUnknownBookOrMember(t *testing.T) {
	l := newTestLibrary()

	if _, err := l.Borrow("alice", "no-such-isbn", epoch); !errors.Is(err, ErrBookNotFound) {
		t.Errorf("unknown ISBN error = %v, want ErrBookNotFound", err)
	}
	if _, err := l.Borrow("no-such-member", "978-1", epoch); !errors.Is(err, ErrMemberNotFound) {
		t.Errorf("unknown member error = %v, want ErrMemberNotFound", err)
	}
}

func TestReturnRestoresCopyAndClosesLoan(t *testing.T) {
	l := newTestLibrary()
	if _, err := l.Borrow("alice", "978-1", epoch); err != nil {
		t.Fatalf("Borrow returned unexpected error: %v", err)
	}

	loan, err := l.Return("alice", "978-1", epoch.Add(time.Hour))
	if err != nil {
		t.Fatalf("Return returned unexpected error: %v", err)
	}
	if loan.ReturnedAt == nil {
		t.Fatal("loan.ReturnedAt is nil after Return")
	}

	book, _ := l.Book("978-1")
	if book.AvailableCopies != 1 {
		t.Errorf("AvailableCopies = %d, want 1", book.AvailableCopies)
	}
	if got := l.ActiveLoanCount("alice"); got != 0 {
		t.Errorf("ActiveLoanCount = %d, want 0", got)
	}
}

func TestReturnNeverBorrowed(t *testing.T) {
	l := newTestLibrary()

	_, err := l.Return("alice", "978-1", epoch)
	if !errors.Is(err, ErrNotBorrowedByMember) {
		t.Errorf("error = %v, want ErrNotBorrowedByMember", err)
	}
}

func TestReturnAlreadyReturned(t *testing.T) {
	l := newTestLibrary()
	if _, err := l.Borrow("alice", "978-1", epoch); err != nil {
		t.Fatalf("Borrow returned unexpected error: %v", err)
	}
	if _, err := l.Return("alice", "978-1", epoch.Add(time.Hour)); err != nil {
		t.Fatalf("first Return returned unexpected error: %v", err)
	}

	_, err := l.Return("alice", "978-1", epoch.Add(2*time.Hour))
	if !errors.Is(err, ErrAlreadyReturned) {
		t.Errorf("second Return error = %v, want ErrAlreadyReturned", err)
	}
}

func TestOverdueFee(t *testing.T) {
	loan := &Loan{BorrowedAt: epoch, DueAt: epoch.Add(LoanPeriod)}

	cases := []struct {
		name string
		asOf time.Time
		want int
	}{
		{"well before due", epoch.Add(7 * 24 * time.Hour), 0},
		{"exactly on due date", loan.DueAt, 0},
		{"one day late", loan.DueAt.Add(24 * time.Hour), DailyLateFeeCents},
		{"six days late", loan.DueAt.Add(6 * 24 * time.Hour), 6 * DailyLateFeeCents},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := OverdueFee(loan, tc.asOf); got != tc.want {
				t.Errorf("OverdueFee() = %d, want %d", got, tc.want)
			}
		})
	}
}

func TestOverdueFeeOnNilOrActiveLoan(t *testing.T) {
	if got := OverdueFee(nil, epoch); got != 0 {
		t.Errorf("OverdueFee(nil, ...) = %d, want 0", got)
	}
}
