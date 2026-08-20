source("src/apriori.R")

failures <- 0

check <- function(desc, actual, expected, tolerance = 1e-9) {
  ok <- if (is.numeric(actual) && is.numeric(expected)) {
    length(actual) == length(expected) && all(abs(actual - expected) < tolerance)
  } else {
    identical(actual, expected)
  }
  if (ok) {
    cat(sprintf("  ok - %s\n", desc))
  } else {
    cat(sprintf("  FAIL - %s\n    expected: %s\n    actual:   %s\n",
                desc, paste(expected, collapse = " "), paste(actual, collapse = " ")))
    failures <<- failures + 1
  }
}

# The canonical Tan/Steinbach/Kumar "Bread, Milk, Diaper, Beer" basket
# example: five transactions, well-known frequent itemsets and rules at
# min_support = 0.6, so results here can be checked against the textbook
# rather than just against the code's own reasoning about itself.
transactions <- list(
  c("Bread", "Milk"),
  c("Bread", "Diaper", "Beer", "Eggs"),
  c("Milk", "Diaper", "Beer", "Cola"),
  c("Bread", "Milk", "Diaper", "Beer"),
  c("Bread", "Milk", "Diaper", "Cola")
)

cat("itemset_key\n")
check("sorts items regardless of input order", itemset_key(c("Milk", "Bread")), "Bread,Milk")
check("single item", itemset_key("Beer"), "Beer")

cat("itemset_support\n")
check("Bread appears in 4/5 transactions", itemset_support("Bread", transactions), 0.8)
check("Eggs appears in 1/5 transactions", itemset_support("Eggs", transactions), 0.2)
check("{Diaper,Beer} appears in 3/5 transactions",
      itemset_support(c("Diaper", "Beer"), transactions), 0.6)
check("an item absent from every transaction has zero support",
      itemset_support("Water", transactions), 0)

cat("get_frequent_itemsets\n")
frequent <- get_frequent_itemsets(transactions, min_support = 0.6)

check("Eggs (support 0.2) is not frequent", is.null(frequent[["Eggs"]]), TRUE)
check("Cola (support 0.4) is not frequent", is.null(frequent[["Cola"]]), TRUE)
check("all four single items above threshold are frequent",
      sort(names(frequent)[vapply(frequent, function(f) length(f$items) == 1, logical(1))]),
      c("Beer", "Bread", "Diaper", "Milk"))
check("exactly four 2-itemsets clear 0.6 support",
      sum(vapply(frequent, function(f) length(f$items) == 2, logical(1))), 4L)
check("{Bread,Diaper,Beer} never becomes frequent -- Bread+Beer alone is only 0.4",
      is.null(frequent[[itemset_key(c("Bread", "Diaper", "Beer"))]]), TRUE)
check("no 3-itemset survives at all: every triple has an infrequent pair inside it",
      sum(vapply(frequent, function(f) length(f$items) >= 3, logical(1))), 0L)
check("{Beer,Diaper} support matches direct count",
      frequent[[itemset_key(c("Beer", "Diaper"))]]$support, 0.6)

cat("generate_candidates prunes using the Apriori property, not just re-scans transactions\n")

# At the very first join (singles -> pairs) subsets_ok can't reject
# anything: a candidate pair's only two subsets are the two singles that
# built it, and both are already frequent by construction. So all
# C(4,2) = 6 pairs of frequent singles are proposed here; it's the
# support ("keep") step in get_frequent_itemsets that later drops
# {Beer,Bread} and {Beer,Milk} down to the 4 that are actually frequent.
level1 <- frequent[vapply(frequent, function(f) length(f$items) == 1, logical(1))]
freq_singles <- lapply(level1, function(f) f$items)
pairs <- generate_candidates(freq_singles, frequent)
check("every combination of frequent singles is proposed -- nothing to prune yet",
      length(pairs), choose(length(freq_singles), 2))

# The subsets_ok prune inside generate_candidates guards a different
# case: two frequent k-itemsets that DO share a join prefix, but whose
# *other* combined subset was never frequent. Construct that directly,
# since this five-transaction dataset happens not to contain one.
synthetic_frequent <- list(
  "A" = list(items = "A", support = 1), "B" = list(items = "B", support = 1),
  "C" = list(items = "C", support = 1),
  "A,B" = list(items = c("A", "B"), support = 1),
  "A,C" = list(items = c("A", "C"), support = 1)
  # "B,C" deliberately absent: never frequent
)
synthetic_pairs <- list(c("A", "B"), c("A", "C"))
blocked <- generate_candidates(synthetic_pairs, synthetic_frequent)
check("a candidate triple is pruned when its non-join subset {B,C} isn't frequent",
      length(blocked), 0L)

synthetic_frequent[["B,C"]] <- list(items = c("B", "C"), support = 1)
allowed <- generate_candidates(synthetic_pairs, synthetic_frequent)
check("the same join succeeds once {B,C} is also frequent",
      vapply(allowed, itemset_key, character(1)), "A,B,C")

cat("generate_rules\n")
rules <- generate_rules(frequent, transactions, min_confidence = 0.6)
rule_label <- function(r) paste0(paste(r$antecedent, collapse = "+"), "=>",
                                  paste(r$consequent, collapse = "+"))
by_label <- setNames(rules, vapply(rules, rule_label, character(1)))

check("{Beer} => {Diaper} holds in every transaction with Beer: confidence 1.0",
      by_label[["Beer=>Diaper"]]$confidence, 1.0)
check("{Diaper} => {Beer} only holds in 3 of Diaper's 4 transactions: confidence 0.75",
      by_label[["Diaper=>Beer"]]$confidence, 0.75)
check("{Beer} => {Diaper} lift: confidence(1.0) / support(Diaper=0.8)",
      by_label[["Beer=>Diaper"]]$lift, 1.0 / 0.8)
check("a rule's support equals the support of its full itemset",
      by_label[["Beer=>Diaper"]]$support, 0.6)

strict_rules <- generate_rules(frequent, transactions, min_confidence = 0.99)
check("raising min_confidence above 0.75 drops {Diaper}=>{Beer}",
      "Diaper=>Beer" %in% vapply(strict_rules, rule_label, character(1)), FALSE)
check("but keeps the confidence-1.0 {Beer}=>{Diaper} rule",
      "Beer=>Diaper" %in% vapply(strict_rules, rule_label, character(1)), TRUE)

no_rules <- generate_rules(frequent, transactions, min_confidence = 1.01)
check("a threshold above 1.0 confidence is unreachable and yields no rules",
      length(no_rules), 0L)

cat("\n")
if (failures > 0) {
  cat(sprintf("%d check(s) failed\n", failures))
  quit(status = 1)
} else {
  cat("all checks passed\n")
}
