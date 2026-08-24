# Implementing a Search Engine (Python)

**Source:** [Implementing a Search Engine](http://www.ardendertat.com/2011/05/30/how-to-implement-a-search-engine-part-1-create-index/)
by Arden Dertat (Parts 1–3: index, query, tf-idf ranking), from the
Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the
finished implementation directly at the project root (no separate
`reference/`). Zero external dependencies — standard library only.

## What it is

- `src/tokenizer.py` — lowercases, strips punctuation (keeping internal
  apostrophes like `it's`), and optionally drops a small stopword list.
  Returns a list, not a set, since term-frequency counting needs the
  duplicates.
- `src/index.py` — `InvertedIndex`: `postings[term][doc_id] = tf`, plus
  a `doc_terms[doc_id] = {term: tf}` reverse view kept alongside it so
  building a per-document tf-idf vector doesn't require scanning the
  entire vocabulary. Built either from a `{doc_id: text}` dict (for
  tests) or a directory of `.txt` files (for the CLI).
- `src/query.py` — a left-to-right boolean evaluator: `AND`/`OR`/`NOT`
  operate on the postings sets directly (`&`, `|`, `-`), no precedence
  or parentheses. A bare multi-word query is an implicit `AND` chain.
- `src/ranking.py` — tf-idf vectors (`1 + ln(tf)` sublinear scaling times
  `ln(N/df)`) and cosine similarity between the query vector and each
  candidate document's vector. Candidates are gathered from the postings
  lists of the query's own terms, not a full scan of every document.
- `src/cli.py` — `search-engine bool <query>` and
  `search-engine search <query> [-k N]` over `corpus/`, a set of 8
  short hand-written documents spanning distinct topics (Python the
  language, snakes the animal, oceans, space, search engines,
  programming languages in general, coffee brewing, marathon training)
  chosen specifically so ranking has to discriminate between overlapping
  vocabulary, not just presence/absence.
- `tests/` — 41 unit tests: tokenizer edge cases, index construction and
  postings correctness, all four boolean-query code paths plus three
  malformed-query error cases, tf-idf/cosine math (including an exact
  cosine-similarity-equals-1.0 case), and five end-to-end tests against
  the real `corpus/` directory.

## Run it

```bash
cd 2026-08-24-python-search-engine
python3 -m unittest discover -v      # 41 tests

python3 -m src.cli search python programming
python3 -m src.cli search deep sea creatures
python3 -m src.cli bool python AND programming
python3 -m src.cli bool coffee OR marathon
```

Actual output:

```
$ python3 -m src.cli search python programming
python_language.txt  (score=0.2954)
    Python is a high-level programming language known for readable syntax.
programming_languages.txt  (score=0.1601)
    A programming language is a formal language used to write instructions

$ python3 -m src.cli search deep sea creatures
ocean_life.txt  (score=0.3432)
    The ocean covers more than seventy percent of the surface of the Earth.

$ python3 -m src.cli bool python AND programming
programming_languages.txt
    A programming language is a formal language used to write instructions
python_language.txt
    Python is a high-level programming language known for readable syntax.

$ python3 -m src.cli search cryptocurrency
no matches
```

## What it actually teaches

- **An inverted index turns "which documents match?" from an O(N) scan
  of every document into an O(1) dict lookup per query term.** `postings`
  maps term → the doc_ids that contain it, built once at index time.
  Boolean search never touches document text again — `docs_containing`
  is a dict lookup, and `AND`/`OR`/`NOT` are just set intersection, union,
  and difference over those results. The tradeoff is visible in
  `doc_terms`: I kept a *second* index (doc_id → its own term counts)
  purely so ranking wouldn't have to invert `postings` back into a
  per-document view every time it needed one — same information, stored
  twice, because the two access patterns (by term, by document) need it
  shaped differently.
- **Raw term frequency alone makes ranking mostly measure document
  length, and log-damping is what fixes that.** My first pass weighted
  by raw `tf`, and `test_doc_dominated_by_query_term_ranks_above_doc_with_mixed_terms`
  initially failed in a way that made this concrete: a document that
  used a query term proportionally *less* of its total content could
  still outscore one that used it proportionally more, just because it
  was longer and had a bigger raw count. Switching to `1 + ln(tf)`
  (the standard "ltc" weighting) compresses the gap between "occurs 20
  times" and "occurs 21 times" while still rewarding "occurs 3 times"
  over "occurs 1 time" — going from zero to one occurrence matters far
  more than any later increment, which raw counts don't express.
- **Cosine similarity ranks by the *angle* between vectors, not their
  magnitude — which is exactly why a single-term query against a
  single-topic document produces exactly 1.0, not just "a high score."**
  `test_single_term_query_matching_pure_doc_is_perfect_cosine` (d3 is
  literally just the word "cherry" repeated) hits this precisely because
  the document vector has only one nonzero component, in the same
  direction as the query vector — dividing by both norms cancels the
  magnitude out entirely and leaves pure direction, which is the whole
  point of normalizing by `‖query‖ · ‖doc‖` instead of just taking a dot
  product.
- **A term that never appears in the corpus has to resolve to "contributes
  nothing," not a crash, and that has to be decided explicitly.**
  `idf(t) = ln(N / df(t))` divides by `df(t)`, which is legitimately zero
  for a query word nothing in the corpus uses — `cryptocurrency` in the
  worked example above. `idf()` special-cases `df == 0` to return `0.0`
  rather than raising `ZeroDivisionError` or `math.log(0)`'s `ValueError`,
  which in turn makes that term's weight `0.0` in `term_vector` (filtered
  out entirely, since the dict comprehension only keeps `idf > 0`), which
  is what makes `test_query_with_only_unknown_terms_returns_nothing` and
  the real "cryptocurrency blockchain mining" CLI example above return a
  clean `no matches` instead of an exception.
- **Boolean query correctness needed as many tests for what should
  *reject* the query as for what should match one.** `test_cannot_start_with_not`,
  `test_cannot_end_with_operator`, and `test_cannot_have_two_operators_in_a_row`
  exist because a left-to-right evaluator with no grammar validation
  will happily do *something* with `NOT python` (try to negate a result
  set that doesn't exist yet) or `python AND` (silently ignore the
  trailing dangling operator) rather than tell the caller their query
  was malformed — raising `BooleanQueryError` at parse time, before any
  set operations run, was a deliberate choice over guessing what the
  caller meant.

## Deliberate scope cuts

- **No query parentheses or operator precedence.** `AND`/`OR`/`NOT` apply
  strictly left to right against the running result set — `"a OR b AND c"`
  is `(a OR b) AND c`, not `a OR (b AND c)`, and there's no way to write
  the latter.
- **No phrase queries or positional postings.** The index stores term
  frequency per document, not term *positions*, so `"machine learning"`
  as an exact adjacent phrase can't be distinguished from the two words
  appearing anywhere in the same document.
- **No stemming.** `"run"`, `"running"`, and `"ran"` are three unrelated
  terms as far as the index is concerned.
- **No persistence.** The index is rebuilt from `corpus/*.txt` on every
  CLI invocation; there's no serialized index file.
- **Corpus is 8 hand-written documents, not a crawled or scraped
  collection** — chosen deliberately to exercise ranking discrimination
  (Python-the-language vs. python-the-snake) rather than to demonstrate
  scale.

## What I'd add next

- **Phrase queries**, by extending postings to store term positions per
  document (`{term: {doc_id: [positions]}}`) instead of just counts, and
  checking positional adjacency at query time.
- **A simple stemmer** (even a hand-rolled suffix-stripper for `-ing`,
  `-ed`, `-s`) so query and document vocabulary overlap more often.
- **Query parentheses**, by replacing the left-to-right evaluator with a
  small recursive-descent parser over the same AND/OR/NOT grammar.

## License

Licensed under the MIT License; see the LICENSE file at the repository
root. Built from ["Implementing a Search Engine"](http://www.ardendertat.com/2011/05/30/how-to-implement-a-search-engine-part-1-create-index/)
by Arden Dertat.
