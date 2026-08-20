# Association Rule Mining (R)

**Source:** ["Learn Associate Rule Mining in R"](https://towardsdatascience.com/association-rule-mining-in-r-ddf2d044ae50),
from the R section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries -- `towardsdatascience.com` gets a flat `EGRESS_BLOCKED`,
and so does CRAN itself, which rules out the tutorial's actual path of
`install.packages("arules")` and calling its `apriori()`. What's here
instead is what that package call is standing in for: the Apriori
algorithm itself (Agrawal & Srikant, 1994), implemented from the published
level-wise search rather than from the tutorial's source, using nothing
but base R (`r-base-core`, installed from the Ubuntu archive mirror, which
*is* reachable).

## What it is

A from-scratch frequent-itemset miner and rule generator over a list of
market-basket transactions, run from the command line -- no `arules`, no
`Matrix`, no S4 classes, just base-R lists and vectors.

- `src/apriori.R` -- four functions, no package dependencies:
  - `itemset_support(items, transactions)` -- fraction of transactions
    containing every item in `items`.
  - `get_frequent_itemsets(transactions, min_support)` -- the level-wise
    Apriori search: start from frequent single items, join survivors into
    one-larger candidates, filter by support, repeat until a level comes
    back empty.
  - `generate_candidates(freq_level, frequent)` -- the join-and-prune step:
    merge pairs of frequent k-itemsets that share a (k-1)-prefix, then
    discard any merge with an infrequent subset before it's ever counted
    against the transactions.
  - `generate_rules(frequent, transactions, min_confidence)` -- for every
    frequent itemset of size >= 2, tries every non-empty antecedent/
    consequent split and keeps the ones clearing `min_confidence`,
    reporting support, confidence, and lift.
- `src/main.R` -- reads a transactions file (one comma-separated basket
  per line), runs both stages, and prints frequent itemsets and rules
  sorted by size and by confidence respectively.
- `resources/transactions.txt` -- the five-transaction `{Bread, Milk,
  Diaper, Beer, Eggs, Cola}` basket example from Tan/Steinbach/Kumar's
  *Introduction to Data Mining*, chosen because its frequent itemsets and
  rules at `min_support=0.6` are textbook-known values, not just
  self-consistent with this code.
- `test/test_apriori.R` -- 24 hand-rolled assertions checked against
  those known textbook values (no test framework: `install.packages` was
  the only route to one, and CRAN isn't reachable here either).

## Run it

```bash
cd 2026-08-20-r-association-rules
make test                                       # 24 assertions
make run                                        # the textbook basket at 0.6/0.75
Rscript src/main.R resources/transactions.txt 0.4 0.6   # looser thresholds
```

## What it actually teaches

- **Apriori's whole speed advantage is refusing to count itemsets that
  *can't* be frequent, and that only pays off starting at triples.**
  `generate_candidates`'s `subsets_ok` check -- every subset of a
  candidate must already be in the frequent table -- is a no-op at the
  first join (singles into pairs): a pair's only two subsets are the two
  singles that built it, both already known-frequent. `test_apriori.R`
  pins this directly: joining the 4 frequent singles proposes all
  `choose(4,2) = 6` pairs, pruning nothing. The real filtering at *that*
  level happens afterward, in the support ("keep") step. The prune only
  starts doing real work at triples and beyond, checked with a synthetic
  `{A,B}`/`{A,C}` pair whose third subset `{B,C}` is deliberately left out
  of the frequent table -- the join is rejected outright, before a single
  transaction is scanned for it.
- **`{Bread,Diaper,Beer}` never gets proposed as a candidate at all, for
  a subtler reason than "it's not frequent."** `{Bread,Beer}` itself is
  only 0.4 support, so it's filtered out at the *pairs* level and never
  makes it into `freq_level` for the triples join -- there's no pair
  starting with `Beer` to combine with `{Bread,Diaper}`'s `Bread`-prefix
  group. The candidate is absent for want of a parent, not because
  `subsets_ok` caught it; `{Bread,Milk,Diaper}` (all three pairwise
  subsets *are* frequent) *does* get proposed and only falls at the final
  support check, at 0.4. Two different itemsets, two different reasons
  neither survives -- the test suite checks both separately rather than
  treating "not in the final frequent set" as one undifferentiated bucket.
- **Confidence is asymmetric in a way support alone can't show.**
  `{Beer,Diaper}` has one support value (0.6), but `{Beer}=>{Diaper}` and
  `{Diaper}=>{Beer}` have different confidences -- 1.00 vs. 0.75 -- because
  confidence divides by the *antecedent's* support (0.6/0.6 vs. 0.6/0.8).
  Every transaction with Beer also has Diaper; not every transaction with
  Diaper has Beer. Lift (1.25 for the confident direction) is what says
  this isn't just "Diaper is common": Beer's presence still raises P(Diaper)
  above its 0.8 baseline.
- **Confidence and lift never need a support recount, by construction.**
  `generate_rules` looks up both the antecedent's and the consequent's
  support directly from the `frequent` table built during the itemset
  search, rather than rescanning `transactions`. That only works because
  of the same anti-monotonicity Apriori itself relies on: any subset of a
  frequent itemset is guaranteed frequent, so it's guaranteed to already
  be a key in that table. The lookup has a `stop()` guard for the case
  that invariant ever breaks, instead of silently falling back to a
  recompute that would hide a real bug.
- **`r-base-core` alone, no `arules`, is enough -- and CRAN was never
  needed.** `apt-get install --no-install-recommends r-base-core` pulls
  from the Ubuntu archive mirror (reachable) rather than CRAN
  (unreachable); the full `r-base` metapackage additionally drags in X11
  and Tk for a GUI front-end this project never touches, so the
  `--no-install-recommends` flag is what keeps the install from failing
  on an unrelated `mesa` package 404.

## Deliberate scope cuts

- **No `arules`-style sparse transaction matrix.** Transactions are plain
  R lists of character vectors; every support check is a linear scan with
  `vapply`. Fine at five or even a few thousand transactions -- the real
  `arules` package exists specifically because that stops being fine at
  the scale actual market-basket datasets reach.
- **Support and confidence only, no other interest measures.** Real
  association-rule tools also offer conviction, leverage, and others;
  this implementation stops at the three (support, confidence, lift) the
  textbook example is built around.
- **No categorical/continuous attribute discretization.** Every "item" is
  already a discrete basket entry (`Bread`, `Milk`, ...); a real dataset
  with numeric columns needs a binning step before Apriori applies at
  all, which is a separate preprocessing concern from the mining
  algorithm itself.

## What I'd add next

- **A hash-tree or trie for candidate storage**, since `frequent` is a
  flat named list keyed by a sorted-and-joined string -- fine for a
  six-item universe, but real Apriori implementations index candidates
  so a transaction scan doesn't have to test itemset membership one
  candidate at a time.
- **FP-Growth as a second miner over the same transactions**, specifically
  to make concrete *why* it exists: Apriori's repeated database scans (one
  full pass per level) are exactly what FP-Growth's compressed tree
  avoids.
- **Rule pruning by redundancy** (dropping a rule when a more general
  version has equal confidence), since right now every antecedent/
  consequent split clearing `min_confidence` is reported independently,
  including near-duplicates that add little beyond a more specific one
  already in the list.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Built from ["Learn Associate Rule Mining in R"](https://towardsdatascience.com/association-rule-mining-in-r-ddf2d044ae50).
