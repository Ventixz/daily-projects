# Pure ledger operations, kept free of Shiny so they can be unit-tested
# without spinning up a reactive session.

new_ledger <- function() {
  data.frame(
    date = as.Date(character()),
    description = character(),
    category = character(),
    amount = numeric(),
    stringsAsFactors = FALSE
  )
}

add_transaction <- function(ledger, date, description, category, amount) {
  if (!inherits(date, "Date")) {
    date <- as.Date(date)
  }
  if (is.na(date)) {
    stop("date must be a valid date")
  }
  if (!is.character(description) || nchar(trimws(description)) == 0) {
    stop("description must not be empty")
  }
  if (!is.character(category) || nchar(trimws(category)) == 0) {
    stop("category must not be empty")
  }
  amount <- suppressWarnings(as.numeric(amount))
  if (is.na(amount) || amount == 0) {
    stop("amount must be a non-zero number")
  }

  row <- data.frame(
    date = date,
    description = trimws(description),
    category = trimws(category),
    amount = amount,
    stringsAsFactors = FALSE
  )
  rbind(ledger, row)
}

remove_transaction <- function(ledger, row_index) {
  if (row_index < 1 || row_index > nrow(ledger)) {
    stop("row_index out of range")
  }
  ledger[-row_index, , drop = FALSE]
}

compute_balance <- function(ledger) {
  if (nrow(ledger) == 0) {
    return(0)
  }
  sum(ledger$amount)
}

running_balance <- function(ledger) {
  if (nrow(ledger) == 0) {
    return(numeric(0))
  }
  ordered <- ledger[order(ledger$date), , drop = FALSE]
  cumsum(ordered$amount)
}

summarize_by_category <- function(ledger) {
  if (nrow(ledger) == 0) {
    return(data.frame(category = character(), total = numeric(), stringsAsFactors = FALSE))
  }
  totals <- tapply(ledger$amount, ledger$category, sum)
  result <- data.frame(
    category = names(totals),
    total = as.numeric(totals),
    stringsAsFactors = FALSE
  )
  result[order(-abs(result$total)), , drop = FALSE]
}
