# Simple Actor-Based Blockchain (Scala)

**Source:** ["Simple actor-based blockchain"](https://www.freecodecamp.org/news/how-to-build-a-simple-actor-based-blockchain-aac1e996c177/),
from the Scala section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries — `freecodecamp.org` gets a flat `403`, so what's here
isn't a port of that specific article. It's the concept its title names:
a toy blockchain where each peer is an actor with its own mailbox and its
own copy of the chain, and consensus is whatever falls out of message
passing plus a longest-valid-chain rule — no shared mutable ledger
anywhere in the program.

## What to build

- A `Block`: index, timestamp, payload, previous hash, nonce, and a
  SHA-256 hash that commits to all of it.
- Proof-of-work mining: brute-force the nonce until the hash has N
  leading hex zeros.
- A `Blockchain` validator: given a `Vector[Block]`, decide whether every
  link's index, `prevHash`, and recomputed hash are internally consistent
  — a pure function, no actor involved.
- A minimal actor runtime: mailbox + dedicated thread + single-threaded
  `receive`, since neither `scala.actors` (removed after 2.10) nor Akka
  (lives on Maven Central, unreachable without `sbt`, see `LEARNING.md`)
  is available here.
- `Node` actors that mine on request, broadcast what they mine, and
  reconcile with peers over messages only: `MineBlock`, `NewBlock`,
  `RequestChain`, `ChainResponse`.

## What it teaches

- An actor is the encapsulation boundary, not just a concurrency
  primitive: two `Node`s can only affect each other by sending a message
  that the other chooses to act on, never by touching the other's `chain`
  field directly — there isn't a way to.
- The longest-chain rule isn't a special case bolted onto blockchain
  logic; it's what a node *has* to do once "ask the other actor" is the
  only tool available for resolving "whose history is right."
- Where proof-of-work actually buys anything: not in one block's hash
  (a single SHA-256 call is just as fast to fake), but in the fact that
  building a longer *valid* chain costs real, cumulative work, which is
  the only thing that makes "prefer the longer chain" a meaningful rule
  instead of an arbitrary one.

## Setup

- Scala 2.11 (`apt-get install scala`) is enough — no build tool, no
  third-party actor or crypto library. `java.security.MessageDigest`
  covers SHA-256; `java.util.concurrent.LinkedBlockingQueue` covers the
  mailbox.

## Milestones

1. `Block` + SHA-256 hashing + proof-of-work mining as pure, testable
   functions with no actor in sight yet.
2. `Blockchain.isValid` over a `Vector[Block]` — index continuity,
   `prevHash` linkage, hash recomputation, difficulty target.
3. A minimal `Actor` base class: mailbox, thread, `!` to send, `receive`
   as a `PartialFunction`.
4. `Node` wired to mine on `MineBlock` and gossip via `NewBlock`.
5. Fork resolution: `RequestChain`/`ChainResponse` so a node behind a
   longer valid chain adopts it wholesale, not block-by-block.
6. A multi-node demo (`Main`) that mines a few rounds across three peers
   and confirms they converge on an identical chain.
