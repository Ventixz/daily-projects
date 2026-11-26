# Learning: Build a Twitter Bot with Clojure (a Markov-chain bot for the X API)

**Source:** "Build a Twitter Bot with Clojure", the *How I Start: Clojure* post
(<http://howistart.org/posts/clojure/1/index.html>). It is listed in the Clojure section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
howistart.org did not resolve from the build environment, so this project follows the post's
**subject**: a bot that writes new sentences with a Markov chain trained on a book and posts them
on a schedule. It does **not** follow the post's text. The post used library wrappers for Twitter
and scheduling. Here the OAuth signing, the HTTP client calls, the JSON and the scheduler use only
Clojure and the JDK.

> Written by the daily-project routine (Claude), not by the repo owner.

## What it is

```
$ make generate ARGS="--count 3"
What a number of little shrill voices.
Why, I hadn't cried so much! I must go by post, as she's got a moral, if only you know — What made you so unhappy.
Consider your verdict, the King repeated angrily, or I'll have you to tell him.
```

- **Markov text** (`markov.clj`). The bot reads *Alice's Adventures in Wonderland* and *Through
  the Looking-Glass* (public domain, about 55,000 words, in `resources/corpus.txt`). For every pair
  of words it counts which words come next. To make a sentence, it starts from a pair that opens a
  sentence in the book and keeps picking a next word at random, weighted by those counts, until it
  reaches a word ending in `.`, `!` or `?`.
- **Originality check.** If every 5-word stretch of a sentence also appears in the book, the
  sentence is just a quote, so the bot throws it away.
- **Tweet length the way X counts it.** X does not count characters. It normalizes text to NFC and
  gives each code point a weight: 1 for Latin-range text, 2 for CJK and emoji. The limit is 280.
- **OAuth 1.0a** (`oauth.clj`). Requests are signed with HMAC-SHA1 following RFC 5849. The test
  suite checks the result against the worked example in X's own documentation.
- **X API v2 client** (`twitter.clj`). It sends `POST /2/tweets` with a JSON body using
  `java.net.http`. Each kind of failure becomes an error with its own `:type`: `:rate-limited` (with
  the reset time), `:duplicate`, `:unauthorized`, `:forbidden` or `:http-error`.
- **Bot loop** (`bot.clj`). The bot does not repeat its last 200 tweets. On a "duplicate" reply it
  tries different text. On a 429 it waits until the reset time. On bad credentials it stops instead
  of failing every interval. Scheduling uses a `ScheduledExecutorService`.
- **CLI** (`core.clj`) with three commands: `generate`, `post [--dry-run]` and
  `run --every MINUTES [--max N]`.

## Run it

You need Java 21 or newer and nothing else.

```
make test                                  # downloads 3 jars from Maven Central (SHA-1 checked), runs 42 tests
make generate ARGS="--count 5 --seed 42"   # print tweets; the same seed gives the same tweets
make run ARGS="post --dry-run"             # one tweet, printed instead of posted
make run ARGS="run --every 60 --dry-run"   # one tweet an hour until Ctrl-C
```

To post for real, create an app in the X developer portal with **Read and write** permission.
Generate an access token and secret *after* setting that permission (a token created earlier stays
read-only). Then set these variables:

```
export TWITTER_API_KEY=...  TWITTER_API_SECRET=...
export TWITTER_ACCESS_TOKEN=...  TWITTER_ACCESS_TOKEN_SECRET=...
make run ARGS="run --every 180 --max 10"
```

`TWITTER_API_BASE` replaces `https://api.twitter.com` if you need another host, such as `api.x.com`.

**Windows 11 / PowerShell:** install Java with `winget install Microsoft.OpenJDK.21` and the
Clojure CLI as described at clojure.org ("Install Clojure", Windows section). Then use the
`deps.edn` aliases:

```
clj -M:test
clj -M:run generate --count 5
$env:TWITTER_API_KEY="..."; clj -M:run post
```

(The `deps.edn` path was not run in the build environment, which has no Clojure CLI. The Makefile
path was.)

You can train the bot on any other text with `--corpus FILE`. `--order K` sets how many words of
context each pick uses (default 2). Order 1 gives wilder text. Order 3 reads more naturally but
copies longer stretches of the book, so the originality check rejects more of its sentences.

## Tests (42 tests, about 2,400 assertions, about 7 s)

- **markov (9):** tokenizing (quotes, brackets, `--`, headings), exact chain counts on a small text,
  and weighted picks landing near 75/25 for 3:1 counts. In 100 generated sentences, every word
  transition has to exist in the chain. 200 composed tweets all have to fit in 280 weighted
  characters, start with a capital, end a sentence and not appear in the book. Also: same seed
  gives same output, a chain that can only reproduce its source returns nil, and smaller limits are
  respected.
- **oauth (7):** RFC 3986 encoding (`*`, `+`, space, `☃`), URL normalization, and **the signature
  base string and signature from X's documentation**, matched exactly
  (`hCtSmYh+iHYCEqBWrE7C7hYmtUk=`). Also the header format, sorting repeated parameters, an empty
  token secret, and nonce uniqueness.
- **json (5):** writing, reading, escapes including a surrogate pair, a round trip, and 12 kinds of
  malformed input.
- **twitter (4):** these run against `test/twitter_bot/fake_x.clj`, a local stand-in for the X API.
  It **checks signatures with its own code** (`URLEncoder` plus fixes, not `oauth.clj`). It rejects
  timestamps more than 5 minutes off and reused nonces, as the real service does. The tests cover a
  successful post, a wrong secret, a stale timestamp, a replayed nonce, and the 429, 403 duplicate,
  403 forbidden, 503 and malformed-201 cases.
- **bot (10):** a post is remembered, 50 ticks give 50 different texts, a duplicate leads to new
  text (giving up after 3), a 429 blocks posting until the reset time (15 minutes if no reset time
  is given), 401 and 403 are fatal while 503 is not, and the scheduler stops at `--max`, on a fatal
  error, or when stopped.
- **core (7):** argument parsing, repeatable `generate`, a custom corpus with `--order 1`, missing
  credentials, a **full `post` against the fake X server**, the CLI's 401 message,
  `run --max 1`, and exit codes.

I checked that the tests can fail by breaking the code on purpose. Encoding `*` wrongly failed 8
tests, because the fake server's separate verifier caught it too. Switching off the originality
check failed 8. Not remembering duplicates failed 1.

## Layout

```
src/twitter_bot/
  markov.clj    tokenize, build-chain, generate-sentence, weighted-length, compose
  oauth.clj     percent-encode, signature base string, HMAC-SHA1, Authorization header
  json.clj      write-str + recursive-descent read-str
  twitter.clj   post-tweet!, classify-response (status -> result or typed ex-info)
  bot.clj       make-bot, tick! (one run, reacting to errors), run-every! (scheduler)
  core.clj      CLI: generate / post / run
test/twitter_bot/
  fake_x.clj    local X API with an independent OAuth verifier
  *_test.clj    one file per namespace; test_runner.clj exits non-zero on failure
resources/corpus.txt   the two Alice books, Gutenberg header and license removed
Makefile  deps.edn
```

## What this project teaches

1. **A Markov chain is a frequency table.** "Train" just means counting:
   `{["the" "Queen"] {"said" 12, "of" 3, ...}}`. "Generate" means a weighted random walk over that
   table. Two design choices matter most:
   - Keep punctuation attached to words (`"Alice,"`, `"said."`). Then the chain learns where commas
     and full stops go without any grammar, and "end of sentence" is just a word that ends in `.`.
   - Remove anything that must be balanced, such as quotes and brackets. A word-level chain has no
     memory beyond its last 2 words, so it will open a quote it never closes.

   The context size (order) trades sense for originality. Order 1 rambles. With order 3 on a
   55,000-word book, most 3-word prefixes have only one follower, so the walk copies long stretches
   of the book. That is why the originality check exists, and why the right order depends on corpus
   size.
2. **Reproducible randomness needs a deterministic structure.** Clojure's hash-map iteration order
   is an implementation detail. If weighted picks walked a map, "same seed, same text" could break
   on a different Clojure version. The chain stores each prefix's followers as a sorted vector with
   running totals, so a pick is one `nextInt` plus a binary search, and the seed fully determines the
   output.
3. **OAuth 1.0a signing is all about exact bytes.** The signature covers
   `METHOD&url&sorted-params`, and every part has to be encoded *exactly* as the server will encode
   it:
   - `java.net.URLEncoder` is wrong here. It turns a space into `+` and leaves `*` alone, and a
     tweet containing either would get a 401.
   - The signing key is `consumer_secret&token_secret`, with both parts encoded, and the `&` stays
     even when the token secret is empty.
   - A JSON body is *not* signed. Only query and form parameters are. So v2's JSON `POST /2/tweets`
     signs only the `oauth_*` parameters.
4. **Test a signer against something other than itself.** If the fake server reused `oauth.clj`
   to verify requests, an encoding bug would sign and verify the same wrong way, and the tests would
   still pass. The fake server has its own encoder. The test also pins the documentation's example
   signature.
5. **Errors that mean different things need different handling.** A scheduled bot that treats
   every non-2xx reply the same way either spams or dies. Here:
   - 429 means "come back at `x-rate-limit-reset`", so the bot keeps that state and sends no
     requests until then.
   - 403 "duplicate" means "say something else".
   - 401, or any other 403, means "retrying won't help", so the bot stops.
   - 5xx means "try at the next interval".

   Making each case an `ex-info` with a `:type` keeps the policy in one `case` in `tick!`.
6. **Tweet length is not `count`.** X uses weighted code points after NFC. An emoji is one code
   point but weighs 2. `e` followed by a combining accent is two UTF-16 units but becomes one code
   point after NFC. The same issue showed up on the console: the JVM's stdout follows the OS locale,
   and a POSIX locale printed every em dash as `?` until `-main` switched to UTF-8 streams.

## Where it differs from the tutorial

- **No libraries.** The post used a Twitter client library and a scheduling library and deployed to
  Heroku. Here each of those is about 100 lines of Clojure/JDK, so the OAuth and the rate-limit
  handling are visible rather than hidden in a dependency.
- **The 2026 API.** v1.1 `statuses/update` (which most 2015-era bots used) has been replaced by v2
  `POST /2/tweets` with a JSON body. Posting needs an app with write permission. The free tier
  allows a small monthly number of posts, so `--every` should be hours, not minutes.
- **Not tested against the live API.** There were no credentials in the build environment. The
  signer matches X's published example, and the fake server verifies signatures independently,
  but the first real post is still the real test. If it gets a 401, check that the access token
  was generated *after* write permission was turned on.

## Ideas for next steps

- Count URLs as 23 characters, as X does, if your corpus contains links.
- Reply to mentions (`GET /2/users/:id/mentions`) by starting the chain from a word in the
  mention.
- Save the chain with `pr-str` / `read-string` so startup does not have to re-read the corpus.
- Mix two corpora, for example Carroll and a programming manual, and weight the transitions from
  each.

## License

Licensed under the MIT License; see the LICENSE file at the repository root. The corpus is public
domain (Lewis Carroll, via Project Gutenberg, with the Gutenberg header and license removed).
