// Command library-demo seeds a tiny library and walks through a borrow,
// a blocked borrow, a return, and an overdue fee, printing each step. It
// exists to give the domain package something runnable outside of tests;
// the actual behavior spec lives in features/*.feature.
package main

import (
	"fmt"
	"time"

	"library-bdd/internal/library"
)

func main() {
	lib := library.New()
	lib.AddBook(library.Book{ISBN: "978-1", Title: "Clean Code", TotalCopies: 1, AvailableCopies: 1})
	lib.AddMember(library.Member{ID: "alice", Name: "Alice", BorrowLimit: 3})
	lib.AddMember(library.Member{ID: "bob", Name: "Bob", BorrowLimit: 3})

	borrowedAt := time.Now()

	loan, err := lib.Borrow("alice", "978-1", borrowedAt)
	report("alice borrows Clean Code", err)

	_, err = lib.Borrow("bob", "978-1", borrowedAt)
	report("bob tries to borrow the same (only) copy", err)

	overdueAsOf := loan.DueAt.Add(3 * 24 * time.Hour)
	fmt.Printf("fee if alice is 3 days late: %d cents\n", library.OverdueFee(loan, overdueAsOf))

	_, err = lib.Return("alice", "978-1", borrowedAt.Add(time.Hour))
	report("alice returns Clean Code", err)

	_, err = lib.Borrow("bob", "978-1", borrowedAt.Add(2*time.Hour))
	report("bob borrows the now-available copy", err)
}

func report(action string, err error) {
	if err != nil {
		fmt.Printf("%s: FAILED (%v)\n", action, err)
		return
	}
	fmt.Printf("%s: ok\n", action)
}
