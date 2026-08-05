# Simple Blockchain (Python)

**Source:** [Build a Simple Blockchain in Python](https://github.com/practical-tutorials/project-based-learning)
(Python: Miscellaneous section), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A minimal proof-of-work blockchain with three pieces:

- `src/block.py` — a `Block` holding an index, timestamp, transaction list, the previous block's
  hash, and a nonce. `compute_hash()` serializes all of that (except the hash itself) through
  `json.dumps(..., sort_keys=True)` and SHA-256.
- `src/blockchain.py` — a `Blockchain` that owns the chain (a plain Python list) and a pending
  transaction pool. `mine_pending_transactions` bundles the pool plus a mining reward into a new
  block and runs `proof_of_work` on it; `is_chain_valid` walks the chain checking three separate
  invariants; `get_balance` derives an address's balance by replaying every transaction in every
  block.
- `src/cli.py` — a REPL (`send`, `mine`, `balance`, `chain`, `pending`, `validate`, `tamper`) so
  mining and validation are directly observable, including a `tamper` command that deliberately
  corrupts a mined block to watch `validate` catch it.

## Run it

```bash
cd 2026-08-05-python-blockchain
python3 -m unittest discover -v      # 11 tests

python3 -m src.cli
chain> send alice bob 10
Queued: alice -> bob: 10.0
chain> mine miner1
Mined Block(index=1, hash=000003a9103c..., prev=09d9be8e537c..., nonce=46123, txs=2)
chain> balance bob
bob: 10.0
chain> validate
VALID: Chain is valid
chain> tamper 1 9999
Tampered block 1 (hash left unrecomputed, like a real attacker)
chain> validate
INVALID: Block 1 hash does not match its own content
```

## What it actually teaches

- **A block's hash is a function of everything except itself.** `compute_hash()` builds its input
  dict from `index`, `timestamp`, `transactions`, `previous_hash`, and `nonce` — deliberately
  excluding `self.hash`, because a field can't be an input to its own digest. That's also exactly
  why tampering is detectable at all: `is_chain_valid` doesn't trust the stored `hash` attribute,
  it recomputes one from the current content and compares (`current.hash != current.compute_hash()`).
  Change any transaction amount after the fact without also updating `nonce`/`hash` and the stored
  hash instantly stops matching what the content actually hashes to. The `tamper` REPL command
  exists specifically to make this visible: it edits `transactions[0]["amount"]` and *deliberately
  leaves the hash alone*, because a real attacker rewriting history doesn't get to ask the chain to
  recompute it for them.
- **"Linked" means the previous hash is baked into the next block's own hash, not just stored
  next to it.** Each block's `previous_hash` is one of the fields `compute_hash()` hashes over, so
  the link isn't a separate pointer that could be swapped out independently — it's load-bearing
  input to the very hash that makes the block valid. `test_breaking_the_previous_hash_link_invalidates_chain`
  has to rewrite *both* `previous_hash` and `hash` together to even reach the "does not link to"
  branch of `is_chain_valid`, because rewriting `previous_hash` alone trips the earlier
  self-consistency check first — proof that the two invariants (self-consistent, and actually
  linked to the real predecessor) are genuinely separate failure modes, not one check wearing two
  names.
- **Proof-of-work is "search for a nonce," not "compute a nonce."** `proof_of_work` has no closed
  form — it just increments `nonce` and recomputes the hash in a loop until the hash happens to
  start with `difficulty` zero bytes. There's no way to jump straight to a valid nonce; the only
  path is brute force, and that asymmetry (cheap to verify a hash starts with `0000`, expensive to
  find a nonce that produces one) is the entire security property Bitcoin-style mining leans on.
  Bumping `difficulty` from 2 to 4 in this implementation visibly multiplies the REPL's `mine`
  latency, which is the whole mechanism in miniature.
- **A ledger's "balance" isn't stored anywhere — it's replayed.** `get_balance` has no balance
  field to read; it walks every block, every transaction, and accumulates debits and credits for
  one address from scratch, every single call. That's deliberately how the reference blockchain
  model works (balances are a derived view, not a source of truth) and it's also why tampering
  with an old transaction silently changes every later balance query — there's no cached number
  anywhere to notice the discrepancy, which is exactly the bug class `is_chain_valid` exists to
  catch independently.

## What I'd add next (stretch goals I skipped for scope)

- **Real signatures.** Transactions currently trust whatever `sender` string is passed in — nothing
  stops the REPL from queuing a transaction "from" an address it doesn't control. A real chain
  would require a signature over the transaction, verifiable against the sender's public key.
- **Persisting the chain to disk.** Right now the whole chain lives in memory and evaporates when
  the REPL exits; there's no `save`/`load`.
- **Dynamic difficulty adjustment.** `difficulty` is fixed at construction time; real proof-of-work
  chains retarget it periodically to hold block time roughly constant as hash power changes.
- **A second node and a longest-chain rule.** This is a single in-memory chain with no networking
  or peer-to-peer consensus — there's nothing here that resolves two nodes disagreeing about which
  chain is canonical.
