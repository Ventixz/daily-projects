# Simple Actor-Based Blockchain (Scala)

**Source:** ["Simple actor-based blockchain"](https://www.freecodecamp.org/news/how-to-build-a-simple-actor-based-blockchain-aac1e996c177/),
from the Scala section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries — `freecodecamp.org` gets a flat `403 Forbidden`, same
as `rcoh.svbtle.com` did for the regex-engine day — so this isn't a port
of that article's code. It's the idea the title names, built from
scratch: a toy blockchain where every peer is an actor with its own
mailbox and its own copy of the chain, and "consensus" is just whatever
falls out of message passing plus a longest-valid-chain rule.

## What it is

- `src/Block.scala` — a block (index, timestamp, payload, `prevHash`,
  `nonce`, `hash`) plus SHA-256 hashing and proof-of-work mining, as pure
  functions with no concurrency anywhere in sight.
- `src/Blockchain.scala` — `isValid(chain, difficulty)`: walks
  consecutive pairs and checks index continuity, `prevHash` linkage, hash
  recomputation, and the difficulty target. Also pure.
- `src/Actor.scala` — the whole actor runtime: an unbounded mailbox
  (`LinkedBlockingQueue[Any]`), one dedicated `Thread` per actor running
  a `while (running) { mailbox.take() match { ... } }` loop, and `!` to
  enqueue a message. About 35 lines total.
- `src/Node.scala` — a peer. `MineBlock` triggers proof-of-work and a
  broadcast; `NewBlock` either extends the local tip and relays it
  onward, or — if it doesn't fit — triggers `RequestChain`; `ChainResponse`
  adopts the reply if it's longer and valid. No node ever reads another
  node's `chain` field; every interaction is a message.
- `src/Main.scala` — three nodes, fully peered, mining one block each in
  turn, converging on an identical chain.
- `test/BlockchainTest.scala` — 14 hand-rolled checks (no ScalaTest, same
  reasoning as the regex-engine day): pure-function checks on `Block` and
  `Blockchain`, plus two full actor-system integration checks that start
  real threads and poll for convergence.

## Run it

```bash
cd 2026-08-28-scala-actor-blockchain
make test   # 14 checks, including two live actor-mailbox scenarios
make run    # three peers mine and gossip three blocks, then print convergence
```

## What it actually teaches

- **An actor is an encapsulation boundary before it's a concurrency
  trick.** Every field on `Node` that matters (`chain`, `peers`) is
  `private`; the *only* thing another `Node` can do to it is enqueue a
  case class onto its mailbox and wait. That's not a style choice layered
  on top of the design — `receive: PartialFunction[Any, Unit]` running on
  one dedicated thread per actor is *why* `chain = chain :+ next` inside
  `Node` never needs a lock. Two actors literally cannot interleave
  writes to the same field, because there's only ever one thread that's
  allowed to touch it.
- **The longest-chain rule isn't blockchain-specific — it's what "ask
  don't look" forces you into.** Once a `Node` can't inspect a peer's
  `chain` directly, "whose history is right" has to be settled entirely
  through `NewBlock` → (doesn't fit) → `RequestChain` → `ChainResponse` →
  (longer and valid? adopt : ignore). `test/BlockchainTest.scala`'s
  `"a node behind a longer valid fork adopts it wholesale"` check builds
  two chains that never shared a single message during mining — `long`
  mines three blocks completely alone, `short` mines one, unrelated —
  then wires them together and checks that `short` ends up with `long`'s
  *exact* chain (`==`, not just matching length), entirely by trading
  four messages.
- **Scala 2.11 has no SAM conversion, and the error only shows up at the
  one call site that needs it.** `new Thread(() => loop())` — the
  natural way to write "run this closure on its own thread" — fails to
  compile with `cannot be applied to (() => Unit)`, because implicit
  `FunctionN → SAM interface` conversion is a 2.12 feature (`-Xexperimental`
  in 2.11 at best). `new Thread(new Runnable { def run(): Unit = loop() })`
  is the fix, and it's the same shape of "the compiler used to do this
  for you" gap that datestamped this whole toolchain as pre-2.12 the
  moment it showed up.
- **A bug in test wiring can look exactly like a bug in the code under
  test — until you read which actor sent the message.** The first draft
  of the fork test wrote `long ! NewBlock(long.currentChain.last, long)`
  to announce `long`'s tip to `short`. That line compiles cleanly and
  type-checks perfectly: `!` takes `Any`, so sending `long` a message
  *about* itself, *to* itself, is completely legal Scala. It just means
  `short` never receives anything, so `short.currentChain.length` sits at
  `2` forever and the test times out. The fix was one token —
  `short ! NewBlock(...)` — but finding it meant noticing that `!`'s
  receiver and a `NewBlock`'s `from` field are two independent things the
  type system won't cross-check for you, and only one line said `long`
  where it should've said `short`.
- **The two integration checks need `awaitTrue`, and that's not
  incidental — it's the same asynchrony the actor model is *for*.**
  `a ! MineBlock(...)` returns immediately; the mining and the broadcast
  happen on `a`'s own thread, on its own schedule. A test that asserted
  `b.currentChain.length == 2` on the very next line would be racing the
  mailbox and would fail intermittently depending on scheduler mood. The
  `while (deadline not passed && !cond) Thread.sleep(20)` poll in
  `TestRunner.awaitTrue` isn't a hack around flakiness — it's the correct
  way to observe an actor system from outside, because "eventually
  consistent" isn't a bug here, it's the whole model.

## Deliberate scope cuts

- **No P2P networking.** `Node`s exchange real, typed messages, but all
  in one JVM via in-memory mailboxes — no sockets, no serialization. The
  actor *pattern* is the point; wiring it across machines is a separate,
  orthogonal problem (and was already covered by earlier network-stack
  and TCP-chat days in this repo).
- **Mining blocks the actor's mailbox.** `MineBlock` runs proof-of-work
  synchronously inside `receive`, so a `Node` can't process `NewBlock`
  from a peer while it's mining its own block. A less "simple" version
  would spawn a worker actor for mining and message the result back,
  keeping the miner's mailbox responsive throughout — a real difference
  in how it'd behave under concurrent load, deliberately cut here.
  `Main`'s one-miner-at-a-time demo sidesteps the failure mode entirely
  (see below) rather than papering over it.
- **The demo mines sequentially, not concurrently.** Three nodes mining
  at the *same* height at the *same* time will legitimately fork — each
  peer might adopt whichever of the three competing blocks reaches it
  first, and with no fourth block ever mined on top, nothing forces
  reconciliation. That's correct blockchain behavior, not a bug, but it
  makes `Main`'s "did everyone converge" check nondeterministic for no
  pedagogical benefit. `Main` mines one block per round instead, so the
  demo output is the same on every run; the fork case is exercised
  deliberately and deterministically in the test suite instead, where a
  real assertion is watching it.
- **No transactions, wallets, or signatures.** Each block just carries a
  `String` payload. Real chains validate transaction structure and
  ownership *inside* each block; that's an orthogonal layer on top of
  "chain of hash-linked blocks," not a bigger version of it.

## What I'd add next

- **An async miner.** Spin mining off into its own worker (thread or
  actor) that messages the result back to its `Node`, so `receive` never
  blocks on proof-of-work and a node stays responsive to gossip while it
  mines.
- **Concurrent-mining fork handling in `Main`**, once mining is async:
  let two nodes race to extend the same tip on purpose, print the fork,
  then mine one more block on top of one branch and show the whole mesh
  reconverge — the scenario the sequential demo deliberately dodges.
- **Difficulty retargeting.** Right now `Difficulty` is a fixed constant;
  a real chain adjusts it based on how fast recent blocks were mined,
  which would make `Block.mine`'s brute-force loop actually load-bearing
  instead of a fixed, fairly cheap constant (4 hex zeros — a few
  thousand hashes on average).
