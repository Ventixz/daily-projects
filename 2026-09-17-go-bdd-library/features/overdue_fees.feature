Feature: Overdue fees
  As a librarian
  I want a loan kept past its due date to accrue a daily fee
  So that members have an incentive to return books on time

  Background:
    Given the library has a book "Clean Code" with ISBN "978-1" and 1 copies
    And a member "alice" with a borrow limit of 3
    And "alice" borrows the book "978-1" on day 0

  Scenario Outline: Calculating late fees at various points after borrowing
    When the fee is calculated <days> days after borrowing
    Then the overdue fee should be <fee> cents

    Examples:
      | days | fee |
      | 7    | 0   |
      | 14   | 0   |
      | 15   | 50  |
      | 20   | 300 |
