Feature: Returning books
  As a library member
  I want to return books I've borrowed
  So that other members can borrow them

  Background:
    Given the library has a book "Clean Code" with ISBN "978-1" and 1 copies
    And a member "alice" with a borrow limit of 3
    And a member "bob" with a borrow limit of 3
    And "alice" borrows the book "978-1"

  Scenario: Returning a borrowed book
    When "alice" returns the book "978-1"
    Then the return should succeed
    And the book "978-1" should have 1 copies available
    And "alice" should have 0 active loans

  Scenario: A returned copy becomes borrowable again
    Given "alice" returns the book "978-1"
    When "bob" borrows the book "978-1"
    Then the borrow should succeed

  Scenario: Returning a book the member never borrowed
    When "bob" returns the book "978-1"
    Then the return should fail with "not borrowed by member"

  Scenario: Returning a book that was already returned
    Given "alice" returns the book "978-1"
    When "alice" returns the book "978-1"
    Then the return should fail with "already returned"
