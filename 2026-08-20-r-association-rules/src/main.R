source("src/apriori.R")

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 1) {
  stop("usage: Rscript src/main.R <transactions-file> [min-support] [min-confidence]")
}
path <- args[1]
min_support <- if (length(args) >= 2) as.numeric(args[2]) else 0.6
min_confidence <- if (length(args) >= 3) as.numeric(args[3]) else 0.75

lines <- readLines(path)
lines <- lines[nzchar(trimws(lines))]
transactions <- lapply(lines, function(l) trimws(strsplit(l, ",")[[1]]))

frequent <- get_frequent_itemsets(transactions, min_support)
rules <- generate_rules(frequent, transactions, min_confidence)

cat(sprintf(
  "Read %d transactions, %d distinct items\n",
  length(transactions), length(unique(unlist(transactions)))
))
cat(sprintf("min_support=%.2f min_confidence=%.2f\n\n", min_support, min_confidence))

cat(sprintf("Frequent itemsets (%d):\n", length(frequent)))
sizes <- vapply(frequent, function(f) length(f$items), integer(1))
labels <- vapply(frequent, function(f) paste(f$items, collapse = ", "), character(1))
for (f in frequent[order(sizes, labels)]) {
  cat(sprintf("  {%s}  support=%.2f\n", paste(f$items, collapse = ", "), f$support))
}

cat(sprintf("\nAssociation rules (%d):\n", length(rules)))
if (length(rules) > 0) {
  confidences <- vapply(rules, function(r) r$confidence, numeric(1))
  for (r in rules[order(-confidences)]) {
    cat(sprintf(
      "  {%s} => {%s}  support=%.2f confidence=%.2f lift=%.2f\n",
      paste(r$antecedent, collapse = ", "), paste(r$consequent, collapse = ", "),
      r$support, r$confidence, r$lift
    ))
  }
}
