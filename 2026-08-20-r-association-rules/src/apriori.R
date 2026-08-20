itemset_key <- function(items) paste(sort(items), collapse = ",")

itemset_support <- function(items, transactions) {
  hits <- vapply(transactions, function(t) all(items %in% t), logical(1))
  sum(hits) / length(transactions)
}

# Join step: merge pairs of frequent (k)-itemsets that share their first
# k-1 sorted items into (k+1)-itemset candidates, then prune any candidate
# that has an infrequent (k)-subset -- the Apriori property: an itemset
# can only be frequent if every one of its subsets is frequent too, so a
# candidate with even one infrequent subset can be discarded without
# ever counting it against the transactions.
generate_candidates <- function(freq_level, frequent) {
  if (length(freq_level) == 0) return(list())
  k <- length(freq_level[[1]])
  candidates <- list()
  seen <- character(0)
  for (i in seq_along(freq_level)) {
    for (j in seq_along(freq_level)) {
      if (i >= j) next
      a <- sort(freq_level[[i]])
      b <- sort(freq_level[[j]])
      prefix_matches <- k == 1 || identical(a[seq_len(k - 1)], b[seq_len(k - 1)])
      if (!prefix_matches) next
      merged <- sort(union(a, b))
      if (length(merged) != k + 1) next
      key <- itemset_key(merged)
      if (key %in% seen) next
      subsets_ok <- all(vapply(seq_along(merged), function(idx) {
        itemset_key(merged[-idx]) %in% names(frequent)
      }, logical(1)))
      if (subsets_ok) {
        candidates[[length(candidates) + 1]] <- merged
        seen <- c(seen, key)
      }
    }
  }
  candidates
}

# Level-wise Apriori search: start from single items, keep only those
# meeting min_support, join survivors into candidates one size larger,
# and repeat until a level produces no frequent itemsets.
get_frequent_itemsets <- function(transactions, min_support) {
  items <- sort(unique(unlist(transactions)))
  level <- as.list(items)
  frequent <- list()

  while (length(level) > 0) {
    supports <- vapply(level, itemset_support, numeric(1), transactions = transactions)
    keep <- supports >= min_support
    freq_level <- level[keep]
    freq_supports <- supports[keep]
    for (idx in seq_along(freq_level)) {
      key <- itemset_key(freq_level[[idx]])
      frequent[[key]] <- list(items = sort(freq_level[[idx]]), support = freq_supports[idx])
    }
    level <- generate_candidates(freq_level, frequent)
  }
  frequent
}

# Every non-empty, proper split of a frequent itemset into
# antecedent/consequent is a candidate rule; confidence and lift both
# read off supports already computed during the frequent-itemset search
# -- by the Apriori property, both halves of any such split are
# themselves frequent, so no support ever needs recomputing here.
generate_rules <- function(frequent, transactions, min_confidence) {
  rules <- list()
  for (key in names(frequent)) {
    itemset <- frequent[[key]]$items
    k <- length(itemset)
    if (k < 2) next
    itemset_support_val <- frequent[[key]]$support

    for (mask in seq_len(2^k - 2)) {
      bits <- as.logical(intToBits(mask))[seq_len(k)]
      antecedent <- sort(itemset[bits])
      consequent <- sort(itemset[!bits])

      ant_key <- itemset_key(antecedent)
      cons_key <- itemset_key(consequent)
      if (is.null(frequent[[ant_key]]) || is.null(frequent[[cons_key]])) {
        stop("subset of a frequent itemset was not itself frequent -- broken invariant")
      }
      ant_support <- frequent[[ant_key]]$support
      cons_support <- frequent[[cons_key]]$support

      confidence <- itemset_support_val / ant_support
      if (confidence < min_confidence) next
      lift <- confidence / cons_support

      rules[[length(rules) + 1]] <- list(
        antecedent = antecedent,
        consequent = consequent,
        support = itemset_support_val,
        confidence = confidence,
        lift = lift
      )
    }
  }
  rules
}
