// Package library is the pure domain model for a small book-lending system.
// It has no I/O and no framework dependency, which is what lets the same
// logic be driven by hand-rolled unit tests and by godog's Gherkin steps.
package library

import (
	"errors"
	"time"
)

const (
	// LoanPeriod is how long a member may keep a book before it is overdue.
	LoanPeriod = 14 * 24 * time.Hour
	// DailyLateFeeCents is charged for each full day a loan is overdue.
	DailyLateFeeCents = 50
)

var (
	ErrBookNotFound        = errors.New("book not found")
	ErrMemberNotFound      = errors.New("member not found")
	ErrNoCopiesAvailable   = errors.New("no copies available")
	ErrBorrowLimitReached  = errors.New("borrow limit reached")
	ErrNotBorrowedByMember = errors.New("not borrowed by member")
	ErrAlreadyReturned     = errors.New("already returned")
)

// Book is a catalog entry. AvailableCopies tracks copies not currently on loan.
type Book struct {
	ISBN            string
	Title           string
	TotalCopies     int
	AvailableCopies int
}

// Member is a library patron with a cap on simultaneous active loans.
type Member struct {
	ID          string
	Name        string
	BorrowLimit int
}

// Loan is one borrow/return cycle of a single copy of a book.
type Loan struct {
	ISBN       string
	MemberID   string
	BorrowedAt time.Time
	DueAt      time.Time
	ReturnedAt *time.Time
}

// IsActive reports whether the copy is still out with the member.
func (l *Loan) IsActive() bool {
	return l.ReturnedAt == nil
}

// Library is an in-memory catalog, member roster, and loan ledger.
type Library struct {
	books   map[string]*Book
	members map[string]*Member
	loans   []*Loan
}

// New returns an empty Library ready for AddBook/AddMember calls.
func New() *Library {
	return &Library{
		books:   make(map[string]*Book),
		members: make(map[string]*Member),
	}
}

// AddBook registers a catalog entry, replacing any existing one with the same ISBN.
func (l *Library) AddBook(b Book) {
	book := b
	l.books[b.ISBN] = &book
}

// AddMember registers a patron, replacing any existing one with the same ID.
func (l *Library) AddMember(m Member) {
	member := m
	l.members[m.ID] = &member
}

// Book looks up a catalog entry by ISBN.
func (l *Library) Book(isbn string) (*Book, bool) {
	b, ok := l.books[isbn]
	return b, ok
}

// ActiveLoanCount returns how many books a member currently has out.
func (l *Library) ActiveLoanCount(memberID string) int {
	count := 0
	for _, loan := range l.loans {
		if loan.MemberID == memberID && loan.IsActive() {
			count++
		}
	}
	return count
}

func (l *Library) findActiveLoan(memberID, isbn string) *Loan {
	for _, loan := range l.loans {
		if loan.MemberID == memberID && loan.ISBN == isbn && loan.IsActive() {
			return loan
		}
	}
	return nil
}

func (l *Library) hasReturnedLoan(memberID, isbn string) bool {
	for _, loan := range l.loans {
		if loan.MemberID == memberID && loan.ISBN == isbn && !loan.IsActive() {
			return true
		}
	}
	return false
}

// Borrow lends a copy of isbn to memberID at time now, enforcing both the
// catalog's copy count and the member's borrow limit.
func (l *Library) Borrow(memberID, isbn string, now time.Time) (*Loan, error) {
	book, ok := l.books[isbn]
	if !ok {
		return nil, ErrBookNotFound
	}
	member, ok := l.members[memberID]
	if !ok {
		return nil, ErrMemberNotFound
	}
	if book.AvailableCopies <= 0 {
		return nil, ErrNoCopiesAvailable
	}
	if l.ActiveLoanCount(memberID) >= member.BorrowLimit {
		return nil, ErrBorrowLimitReached
	}

	book.AvailableCopies--
	loan := &Loan{
		ISBN:       isbn,
		MemberID:   memberID,
		BorrowedAt: now,
		DueAt:      now.Add(LoanPeriod),
	}
	l.loans = append(l.loans, loan)
	return loan, nil
}

// Return closes a member's active loan of isbn at time now. Returning a copy
// that was never borrowed, or one already returned, are distinct errors so
// callers (and their tests) can tell the two mistakes apart.
func (l *Library) Return(memberID, isbn string, now time.Time) (*Loan, error) {
	loan := l.findActiveLoan(memberID, isbn)
	if loan == nil {
		if l.hasReturnedLoan(memberID, isbn) {
			return nil, ErrAlreadyReturned
		}
		return nil, ErrNotBorrowedByMember
	}

	returnedAt := now
	loan.ReturnedAt = &returnedAt
	if book, ok := l.books[isbn]; ok {
		book.AvailableCopies++
	}
	return loan, nil
}

// OverdueFee returns the late fee, in cents, for loan if it were evaluated at
// asOf. A loan due on asOf exactly, or not yet due, owes nothing -- only full
// days strictly past DueAt count.
func OverdueFee(loan *Loan, asOf time.Time) int {
	if loan == nil || !asOf.After(loan.DueAt) {
		return 0
	}
	lateDays := int(asOf.Sub(loan.DueAt) / (24 * time.Hour))
	return lateDays * DailyLateFeeCents
}
