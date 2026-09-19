source(file.path("..", "..", "R", "ledger.R"))

test_that("new_ledger starts empty", {
  l <- new_ledger()
  expect_equal(nrow(l), 0)
  expect_equal(compute_balance(l), 0)
})

test_that("add_transaction appends a validated row", {
  l <- new_ledger()
  l <- add_transaction(l, "2026-09-01", "Paycheck", "Salary", 2000)
  l <- add_transaction(l, "2026-09-02", "Groceries", "Food", -75.50)
  expect_equal(nrow(l), 2)
  expect_equal(compute_balance(l), 1924.5)
})

test_that("add_transaction rejects invalid input", {
  l <- new_ledger()
  expect_error(add_transaction(l, "2026-09-01", "", "Food", -10), "description")
  expect_error(add_transaction(l, "2026-09-01", "Coffee", "", -10), "category")
  expect_error(add_transaction(l, "2026-09-01", "Coffee", "Food", 0), "amount")
  expect_error(add_transaction(l, "2026-09-01", "Coffee", "Food", "nope"), "amount")
  expect_error(add_transaction(l, "not-a-date", "Coffee", "Food", -10))
})

test_that("remove_transaction drops the requested row", {
  l <- new_ledger()
  l <- add_transaction(l, "2026-09-01", "Paycheck", "Salary", 2000)
  l <- add_transaction(l, "2026-09-02", "Groceries", "Food", -75.50)
  l <- remove_transaction(l, 1)
  expect_equal(nrow(l), 1)
  expect_equal(l$description, "Groceries")
})

test_that("remove_transaction rejects out-of-range indices", {
  l <- new_ledger()
  l <- add_transaction(l, "2026-09-01", "Paycheck", "Salary", 2000)
  expect_error(remove_transaction(l, 0), "out of range")
  expect_error(remove_transaction(l, 2), "out of range")
})

test_that("running_balance accumulates in date order regardless of insertion order", {
  l <- new_ledger()
  l <- add_transaction(l, "2026-09-05", "Rent", "Housing", -1000)
  l <- add_transaction(l, "2026-09-01", "Paycheck", "Salary", 2000)
  expect_equal(running_balance(l), c(2000, 1000))
})

test_that("summarize_by_category aggregates and sorts by magnitude", {
  l <- new_ledger()
  l <- add_transaction(l, "2026-09-01", "Paycheck", "Salary", 2000)
  l <- add_transaction(l, "2026-09-02", "Groceries", "Food", -75)
  l <- add_transaction(l, "2026-09-03", "Snacks", "Food", -25)
  summary <- summarize_by_category(l)
  expect_equal(summary$category, c("Salary", "Food"))
  expect_equal(summary$total, c(2000, -100))
})

test_that("summarize_by_category handles an empty ledger", {
  l <- new_ledger()
  summary <- summarize_by_category(l)
  expect_equal(nrow(summary), 0)
})
