Feature: Borrowing books
  As a library member
  I want to borrow available books
  So that I can read them

  Background:
    Given the library has a book "Clean Code" with ISBN "978-1" and 1 copies
    And a member "alice" with a borrow limit of 3

  Scenario: Borrowing an available book
    When "alice" borrows the book "978-1"
    Then the borrow should succeed
    And the book "978-1" should have 0 copies available
    And "alice" should have 1 active loans

  Scenario: Borrowing when no copies are left
    Given "alice" borrows the book "978-1"
    And a member "bob" with a borrow limit of 3
    When "bob" borrows the book "978-1"
    Then the borrow should fail with "no copies available"

  Scenario: Borrowing beyond the member's limit
    Given the library has a book "The Hobbit" with ISBN "978-2" and 5 copies
    And the library has a book "Dune" with ISBN "978-3" and 5 copies
    And a member "dave" with a borrow limit of 2
    And "dave" borrows the book "978-1"
    And "dave" borrows the book "978-2"
    When "dave" borrows the book "978-3"
    Then the borrow should fail with "borrow limit reached"

  Scenario: Borrowing a title that isn't in the catalog
    When "alice" borrows the book "no-such-isbn"
    Then the borrow should fail with "book not found"
