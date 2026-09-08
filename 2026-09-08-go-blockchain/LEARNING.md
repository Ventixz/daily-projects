# Learning: Building Blockchain in Go (Go)

**Source:** ["Building Blockchain in Go"](https://jeiwan.net/posts/building-blockchain-in-go-part-1/)
by Ivan Kuznetsov — Part 1 (Basic Prototype), Part 2 (Proof of Work), and Part 3
(Persistence and CLI), from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Picked and built end-to-end in one sitting, so this folder contains the finished
implementation directly at the project root.

## What it is

A minimal proof-of-work blockchain and a CLI to drive it:

- `internal/blockchain/block.go` — `Block`: timestamp, data, the previous block's
  hash, its own hash, and the nonce that made that hash valid. `NewBlock` mines
  the block as part of construction — there's no way to get an unmined `Block`
  out of this package. `Serialize`/`DeserializeBlock` gob-encode it for storage.
- `internal/blockchain/pow.go` — `ProofOfWork`: mining is "increment a nonce,
  hash the block's header bytes plus that nonce, stop when the hash read as a
  256-bit integer is below a target." `targetBits = 16` means the first 16 bits
  (4 hex digits) of every valid hash must be zero.
- `internal/blockchain/blockchain.go` — `Blockchain`: a slice of blocks plus
  `AddBlock` (mine onto the current tip), `IsValid` (walk the chain checking
  both proof-of-work and hash-linkage), and gob-based `SaveToFile`/`LoadFromFile`
  so the chain survives between CLI invocations.
- `main.go` — `addblock -data "..."` and `printchain`, backed by a
  `blockchain.db` file in the working directory.
- 15 tests across three `_test.go` files, run with `-race`.

## Run it

```bash
cd 2026-09-08-go-blockchain
make test                              # 15 tests
make race                              # same suite, -race
make build
./bin/blockchain addblock -data "Send 1 BTC to Ivan"
./bin/blockchain addblock -data "Send 2 more BTC to Ivan"
./bin/blockchain printchain
```

Each `addblock` call is a separate process that loads `blockchain.db`, mines one
block onto it, and re-saves — `printchain` afterward shows all three blocks
(genesis + 2) with `PoW valid: true` on each, and every `Hash` starting `0000...`.

## What it actually teaches

- **Proof-of-work is a search problem disguised as a hash function.** There's no
  clever formula for "find a nonce that makes this hash small" — `Run` just tries
  nonce `0`, `1`, `2`, ... and hashes the whole candidate block each time, because
  SHA-256 is designed so that changing the input by 1 produces an unpredictable
  output. `TestRunProducesHashBelowTarget` and `TestDifferentDataProducesDifferentHash`
  together pin down that this is genuinely brute-force (different data needs a
  different, unrelated nonce) rather than something solvable in closed form.
- **The difficulty target and the block's own claimed difficulty both have to be
  hashed in, or `Validate` can be fooled.** `prepareData` includes `targetBits`
  itself, not just the block's fields — otherwise a block mined once under a low
  difficulty could be replayed as if it satisfied a higher one, since `Validate`
  only re-hashes what `prepareData` chose to include. This project hardcodes
  `targetBits` as a constant rather than storing it per-block, which is a
  simplification: a real chain like Bitcoin's stores the difficulty a block was
  mined at, because difficulty changes over time.
- **`Validate` has to check two different things, and checking only one is a
  bug that stays invisible until someone tampers with the *other* one.**
  `TestValidateRejectsTamperedData` and `TestValidateRejectsTamperedNonce` mutate
  different fields on purpose — the first proves the hash actually commits to
  `Data` (not just `Nonce`), the second proves it commits to `Nonce` too. Either
  test alone would pass against a `Validate` that only checked the target and
  forgot to re-derive the hash from the stored fields at all.
- **Chain integrity is two separate invariants, not one.** `IsValid` fails a
  chain either when a block's own proof-of-work no longer checks out, or when
  its `PrevBlockHash` doesn't match the previous block's actual `Hash` — those
  are independent failure modes. `TestIsValidCatchesADataSwapBetweenBlocks`
  swaps a block's `Data` without touching its neighbors' links (so `PrevBlockHash`
  chaining stays intact, but that block's own PoW breaks); `TestIsValidCatchesABrokenLink`
  does the opposite (breaks a link, leaves every individual block's PoW valid).
  A validator that only checked one of the two would pass exactly one of these
  and fail the other.
- **Persistence across separate CLI invocations means the chain has to be
  reloaded and re-saved on *every* call, not kept in memory between them.** Each
  `addblock` is its own OS process — `openOrCreateChain` in `main.go` calls
  `LoadFromFile` at the start of every invocation and `SaveToFile` at the end,
  which is what makes running `addblock` twice in a row actually extend one
  chain instead of each call quietly building its own throwaway two-block chain.
  `TestSaveAndLoadRoundTrip` checks the file format survives the trip; the CLI
  session in "Run it" above is what actually exercises two processes sharing
  state through it.

## Deliberate scope cuts

- **File persistence via gob, not BoltDB.** The original tutorial's Part 3 uses
  an embedded key-value store (BoltDB); this build uses a single gob-encoded
  file instead, to keep the project dependency-free (`go.mod` has no `require`
  lines at all). The tradeoff: no indexed lookup by hash, no partial writes —
  every `SaveToFile` rewrites the whole chain.
- **No transactions or UTXO set.** This is blocks-and-hashes only (Part 1-3 of
  the source series); the later parts building a wallet, transactions, and
  address-based balances are out of scope.
- **No networking.** Single process, single chain, no peers to sync with
  (that's Part 7 of the original series).
- **Fixed difficulty.** `targetBits` is a constant, not something that adjusts
  based on how fast recent blocks were mined.

## What I'd add next

- **Difficulty retargeting** — track how long the last N blocks took to mine
  and adjust `targetBits` to hold a target block time, the way real proof-of-work
  chains do, to see whether `prepareData`'s "hash in the difficulty" trick from
  above still holds when difficulty is a per-block value instead of a constant.
- **A UTXO-based transaction model** on top of the existing block structure, to
  see whether `Data []byte` as a free-form payload survives becoming a list of
  structured transactions or needs to be replaced.
- **Concurrent mining with cancellation** — right now `Run` is a single-threaded
  loop with no way to stop it early if a peer's block arrives first, which is
  the point where the single-writer assumption in this build would start to
  break.
