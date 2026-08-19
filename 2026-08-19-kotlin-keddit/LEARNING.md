# Keddit: A Reddit-Style Ranking Engine (Kotlin)

**Source:** ["Keddit - Learn Kotlin While Developing an Android
Application"](https://medium.com/@juanchosaravia/learn-kotlin-while-developing-an-android-app-introduction-567e21ff9664),
from the Kotlin section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries -- `medium.com` gets a flat `403`, and so does
`reddit.com` itself -- so what's here isn't a port of an Android app (the
Android SDK and an emulator aren't available headless here either way).
It's the part of "build a Reddit clone" that's actually interesting once
you strip away the UI: the three ranking algorithms reddit itself
open-sourced years ago (`hot`, the Wilson-score `best`/"confidence", and
`controversial`), reimplemented from the published formulas, well-known
enough to rebuild from memory without the source reachable.

## What it is

A vote-ranking engine for posts and threaded comments, with no
persistence, no HTTP, and no Android -- just the scoring math and the tree
it operates over, driven by a small stdin command language so a whole demo
is a readable text file.

- `src/Ranking.kt` -- three pure functions, each taking only `(ups, downs,
  createdAt)`:
  - `hot` -- reddit's actual public formula: `sign(ups-downs) *
    log10(max(|ups-downs|, 1)) + secondsSinceRedditEpoch / 45000`. Vote
    magnitude contributes logarithmically; time contributes linearly.
  - `confidence` ("best") -- the Wilson score lower bound (95% confidence)
    on the true upvote proportion, treating each vote as a Bernoulli
    trial. Punishes small sample sizes instead of trusting a raw ratio.
  - `controversial` -- `(ups+downs)^balance`, where `balance` is the
    minority side's fraction of the majority side. Zero unless both sides
    have at least one vote.
- `src/Models.kt` -- `Post` and `Comment` data classes (comments carry a
  nullable `parentId` for threading), plus `CommentNode` for the built
  tree.
- `src/Store.kt` -- `Keddit`, an in-memory store: add posts/comments, vote,
  list posts under any of the four sorts (`top` is plain net score, not in
  `Ranking.kt` since it needs no time or Wilson math), and build a post's
  comments into a tree where *every* level -- not just the top one -- is
  independently sorted by the requested order.
- `src/Main.kt` -- reads commands from stdin (`post`, `comment`, `vote`,
  `list <sort>`, `comments <postId> <sort>`) and prints as it goes, so a
  vote's effect on ranking is visible mid-script.
- `test/Tests.kt` -- 25 hand-rolled assertions (see the Clojure and Scala
  days for why: no test framework was reachable to install without a
  network `sbt`/`lein`-style resolve, and a `main` with an `exitProcess(1)`
  on any failure is a complete substitute at this scale).

## Run it

```bash
cd 2026-08-19-kotlin-keddit
make test                       # 25 assertions
make run < resources/demo.txt   # a worked example: an old vs. a new post,
                                 # a comment thread, then votes that visibly
                                 # reorder both
```

## What it actually teaches

- **`hot` is a sum of a logarithmic term and a linear term, and that's the
  whole design.** The vote term (`sign * log10(|ups-downs|)`) means going
  from 10 net votes to 100 only moves the score by `log10(10) = 1` --
  `testHot`'s `at100 - at10` assertion pins that exactly. The time term
  (`secondsSinceEpoch / 45000`) is linear, so *one order of magnitude of
  votes* buys a fixed, calculable amount of "effective age" -- in this
  formula, `45000` seconds (12.5 hours) per order of magnitude. That's
  reddit's actual tuning constant, not an arbitrary round number picked
  for this implementation.
- **A story doesn't out-vote its way to the top forever -- age tax is
  unbounded, votes aren't.** `testStoreVoting` builds this concretely: two
  posts 135,000 seconds (37.5h, exactly 3 × 45000) apart start with p1 (the
  newer one) ahead. Closing a 3-unit gap needs `log10(netVotes)` to grow by
  3, i.e. going from ~1 to ~1000+ net upvotes -- the test casts 2000 votes
  to get there with margin. Ask the same formula to close a *30*-unit gap
  (a month's age difference) and it needs 10^30 net votes: past a certain
  age, no realistic vote count moves a post back into `hot`'s front page.
  That's not a bug in the math, it's the whole mechanism working as
  designed.
- **`confidence` is what keeps "1 upvote, 0 downvotes" from beating "95
  upvotes, 5 downvotes."** Sorting by raw ratio, a single upvote is a
  perfect 100% score and would rank above a 95%-positive comment with 100
  votes behind it. `testConfidence`'s `bigSample > tinySample` assertion is
  the whole point of using a confidence *interval* instead of a point
  estimate: with one vote, the interval covering the plausible true
  approval rate is huge, so its lower bound sits low; with 100 votes at
  95%, the interval is tight and its lower bound sits close to 0.95. More
  evidence at the *same* ratio (`9/1` vs `90/10`, both 90%) still raises
  the score, because the interval narrows either way.
- **`controversial` needs opposition on both sides by construction, not by
  a special-cased check.** `balance = minority/majority` is exactly `0`
  when either side is `0`, so `magnitude^0 = 1`... except the code
  short-circuits to a hard `0.0` before that, because `1` would rank an
  all-upvoted post with a huge magnitude (say, 10,000 ups) above a
  genuinely 50/50-split post with only 100 votes -- `x^0 = 1` for *any*
  `x`, which would make "controversial" mean "popular." The early return
  is what keeps the formula honest to its name.
- **Sorting a comment tree isn't one sort, it's one sort per subtree.**
  `Store.commentTree`'s `build(parentId)` recurses and sorts the children
  at *every* call, not just at the top. `testCommentTree` catches what
  breaks if that recursion is flattened to a single top-level sort: bob's
  two replies (`c3`, `c4`) would either stay in insertion order or get
  interleaved with unrelated top-level comments instead of ranking against
  only their siblings under `c1`.
- **`kotlinc` needs nothing beyond a classpath jar, the same shape of fix
  every JVM day this month has landed on.** `apt-get install kotlin` pulls
  a 1.3.31 compiler and `/usr/share/java/kotlin-stdlib.jar` as plain files;
  `kotlinc src/*.kt -d out` and `java -cp out:kotlin-stdlib.jar
  keddit.MainKt` run with zero network access. The one wrinkle: the
  `kotlin` *launcher script* apt installs is broken (`no build.txt was
  found` -- it expects a differently-laid-out install tree than the
  Debian package ships), so the Makefile invokes `java -cp` directly
  instead of trusting `kotlin` to find its own runtime.

## Deliberate scope cuts

- **No persistence.** Everything lives in an in-memory `LinkedHashMap` for
  the process's lifetime; a real Keddit would need a database, which is
  orthogonal to the ranking math this exercise is actually about.
- **No user accounts or one-vote-per-user enforcement.** `vote` just
  increments a counter; nothing stops the same caller from voting twice.
  Real reddit's actual anti-abuse logic (dedup by account, vote-fuzzing
  against scraping) is a separate, much larger problem from *how a score
  is computed once you trust the vote count*.
- **No Android UI, obviously.** The original tutorial's actual subject --
  Activities, RecyclerViews, Retrofit calls to Reddit's API -- is replaced
  entirely by a stdin command language, since none of that is reachable or
  runnable headless here.

## What I'd add next

- **A real `find` for the front page**, i.e. pagination with a stable
  cursor, since right now `listPosts` re-sorts and returns everything on
  every call -- fine at this scale, not at reddit's.
- **Per-user vote tracking**, specifically to make "no double-voting" a
  real constraint instead of a documented gap, and to make `controversial`
  meaningful against unique voters rather than raw vote events.
- **A decay-aware `best`**, since Wilson-score `confidence` as implemented
  here doesn't consider recency at all -- an old comment with 10,000 votes
  at 90% will outrank a 10-minute-old comment forever, which is a genuinely
  different tradeoff from `hot`'s explicit age term.

Licensed under the MIT License; see the LICENSE file at the repository root.
Built from ["Keddit - Learn Kotlin While Developing an Android
Application"](https://medium.com/@juanchosaravia/learn-kotlin-while-developing-an-android-app-introduction-567e21ff9664)
by @juanchosaravia.
