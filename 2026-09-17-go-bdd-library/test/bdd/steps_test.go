// Package bdd wires the Gherkin scenarios under features/ to the pure
// domain logic in internal/library. Each step is a thin adapter: it never
// contains business rules of its own, only translation from "alice borrows
// 978-1" into a library.Library call and a recorded outcome.
package bdd

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/cucumber/godog"

	"library-bdd/internal/library"
)

// epoch anchors the "on day N" / "N days after borrowing" steps to a fixed
// point in time, so scenarios never depend on the wall clock.
var epoch = time.Date(2026, 1, 1, 9, 0, 0, 0, time.UTC)

// world holds all state for a single scenario. godog reuses one process
// across scenarios, so a fresh world is installed in the Before hook to
// keep scenarios isolated from each other.
type world struct {
	lib *library.Library

	lastErr  error
	lastLoan *library.Loan
	lastFee  int
}

func (w *world) libraryHasBook(title, isbn string, copies int) error {
	w.lib.AddBook(library.Book{
		ISBN:            isbn,
		Title:           title,
		TotalCopies:     copies,
		AvailableCopies: copies,
	})
	return nil
}

func (w *world) aMemberWithBorrowLimit(id string, limit int) error {
	w.lib.AddMember(library.Member{ID: id, Name: id, BorrowLimit: limit})
	return nil
}

func (w *world) borrows(memberID, isbn string) error {
	loan, err := w.lib.Borrow(memberID, isbn, epoch)
	w.lastLoan, w.lastErr = loan, err
	return nil
}

func (w *world) borrowsOnDay(memberID, isbn string, day int) error {
	loan, err := w.lib.Borrow(memberID, isbn, epoch.AddDate(0, 0, day))
	w.lastLoan, w.lastErr = loan, err
	return nil
}

func (w *world) returns(memberID, isbn string) error {
	loan, err := w.lib.Return(memberID, isbn, epoch)
	w.lastLoan, w.lastErr = loan, err
	return nil
}

func (w *world) theBorrowShouldSucceed() error {
	if w.lastErr != nil {
		return fmt.Errorf("expected borrow to succeed, got error: %w", w.lastErr)
	}
	return nil
}

func (w *world) theReturnShouldSucceed() error {
	if w.lastErr != nil {
		return fmt.Errorf("expected return to succeed, got error: %w", w.lastErr)
	}
	return nil
}

func (w *world) theActionShouldFailWith(want string) error {
	if w.lastErr == nil {
		return fmt.Errorf("expected failure %q, but the action succeeded", want)
	}
	if w.lastErr.Error() != want {
		return fmt.Errorf("error = %q, want %q", w.lastErr.Error(), want)
	}
	return nil
}

func (w *world) bookShouldHaveCopiesAvailable(isbn string, want int) error {
	book, ok := w.lib.Book(isbn)
	if !ok {
		return fmt.Errorf("no such book %q", isbn)
	}
	if book.AvailableCopies != want {
		return fmt.Errorf("AvailableCopies = %d, want %d", book.AvailableCopies, want)
	}
	return nil
}

func (w *world) memberShouldHaveActiveLoans(memberID string, want int) error {
	if got := w.lib.ActiveLoanCount(memberID); got != want {
		return fmt.Errorf("ActiveLoanCount(%q) = %d, want %d", memberID, got, want)
	}
	return nil
}

func (w *world) theFeeIsCalculatedDaysAfterBorrowing(days int) error {
	if w.lastLoan == nil {
		return fmt.Errorf("no loan to calculate a fee for")
	}
	asOf := w.lastLoan.BorrowedAt.AddDate(0, 0, days)
	w.lastFee = library.OverdueFee(w.lastLoan, asOf)
	return nil
}

func (w *world) theOverdueFeeShouldBeCents(want int) error {
	if w.lastFee != want {
		return fmt.Errorf("overdue fee = %d cents, want %d cents", w.lastFee, want)
	}
	return nil
}

// InitializeScenario registers every step definition and installs a fresh
// world before each scenario runs.
func InitializeScenario(sc *godog.ScenarioContext) {
	var w *world

	sc.Before(func(ctx context.Context, _ *godog.Scenario) (context.Context, error) {
		w = &world{lib: library.New()}
		return ctx, nil
	})

	sc.Step(`^the library has a book "([^"]*)" with ISBN "([^"]*)" and (\d+) copies?$`, func(title, isbn string, copies int) error {
		return w.libraryHasBook(title, isbn, copies)
	})
	sc.Step(`^a member "([^"]*)" with a borrow limit of (\d+)$`, func(id string, limit int) error {
		return w.aMemberWithBorrowLimit(id, limit)
	})
	sc.Step(`^"([^"]*)" borrows the book "([^"]*)" on day (\d+)$`, func(memberID, isbn string, day int) error {
		return w.borrowsOnDay(memberID, isbn, day)
	})
	sc.Step(`^"([^"]*)" borrows the book "([^"]*)"$`, func(memberID, isbn string) error {
		return w.borrows(memberID, isbn)
	})
	sc.Step(`^"([^"]*)" returns the book "([^"]*)"$`, func(memberID, isbn string) error {
		return w.returns(memberID, isbn)
	})
	sc.Step(`^the borrow should succeed$`, func() error {
		return w.theBorrowShouldSucceed()
	})
	sc.Step(`^the return should succeed$`, func() error {
		return w.theReturnShouldSucceed()
	})
	sc.Step(`^the (?:borrow|return) should fail with "([^"]*)"$`, func(want string) error {
		return w.theActionShouldFailWith(want)
	})
	sc.Step(`^the book "([^"]*)" should have (\d+) copies available$`, func(isbn string, want int) error {
		return w.bookShouldHaveCopiesAvailable(isbn, want)
	})
	sc.Step(`^"([^"]*)" should have (\d+) active loans?$`, func(memberID string, want int) error {
		return w.memberShouldHaveActiveLoans(memberID, want)
	})
	sc.Step(`^the fee is calculated (\d+) days after borrowing$`, func(days int) error {
		return w.theFeeIsCalculatedDaysAfterBorrowing(days)
	})
	sc.Step(`^the overdue fee should be (\d+) cents$`, func(want int) error {
		return w.theOverdueFeeShouldBeCents(want)
	})
}

// TestFeatures is the single entry point go test uses to run every .feature
// file under features/ through the steps above.
func TestFeatures(t *testing.T) {
	suite := godog.TestSuite{
		ScenarioInitializer: InitializeScenario,
		Options: &godog.Options{
			Format:   "pretty",
			Paths:    []string{"../../features"},
			TestingT: t,
		},
	}
	if suite.Run() != 0 {
		t.Fatal("non-zero status returned, failed to run feature tests")
	}
}
