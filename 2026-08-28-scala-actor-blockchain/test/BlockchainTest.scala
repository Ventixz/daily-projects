package chain

/** Hand-rolled assertions, no ScalaTest -- see LEARNING.md for why. */
object TestRunner {
  private var failed = 0
  private var total  = 0

  def check(name: String, cond: => Boolean): Unit = {
    total += 1
    if (!cond) {
      failed += 1
      println(s"FAIL: $name")
    }
  }

  def awaitTrue(timeoutMs: Long)(cond: => Boolean): Boolean = {
    val deadline = System.currentTimeMillis() + timeoutMs
    while (System.currentTimeMillis() < deadline && !cond) Thread.sleep(20)
    cond
  }

  def main(args: Array[String]): Unit = {
    val D = 4 // difficulty shared by every check below

    // --- Block / hashing -----------------------------------------------

    check(
      "genesis hash is deterministic",
      Block.genesis.hash == Block.hashOf(0L, 0L, "genesis", Block.GenesisHash, 0L)
    )

    val mined = Block.mine(1L, "payload", Block.genesis.hash, D)
    check("mined block meets the difficulty target", mined.hash.startsWith("0" * D))
    check("mined block links to its stated parent", mined.prevHash == Block.genesis.hash)
    check("mined block's hash recomputes from its own fields", Block.isValidLink(Block.genesis, mined, D))

    val wrongNonce = mined.copy(nonce = mined.nonce + 1)
    check("a stale nonce breaks the hash check", !Block.isValidLink(Block.genesis, wrongNonce, D))

    // --- Blockchain validation -------------------------------------------

    val chain3 = {
      val b1 = Block.mine(1L, "one", Block.genesis.hash, D)
      val b2 = Block.mine(2L, "two", b1.hash, D)
      Vector(Block.genesis, b1, b2)
    }
    check("a chain mined block-by-block from genesis is valid", Blockchain.isValid(chain3, D))

    val tampered = chain3.updated(1, chain3(1).copy(data = "tampered"))
    check(
      "editing a block's data invalidates every block built on top of it",
      !Blockchain.isValid(tampered, D)
    )

    val reordered = Vector(chain3(0), chain3(2), chain3(1))
    check("a chain out of index order is invalid", !Blockchain.isValid(reordered, D))

    check("the empty-genesis-only vector is not a valid chain", !Blockchain.isValid(Vector.empty, D))
    check("a single genesis block is trivially valid", Blockchain.isValid(Vector(Block.genesis), D))

    // --- Actor wiring: two directly-connected peers propagate a block ----

    run("mining on one node propagates to its peer over the mailbox") {
      val a = new Node("a", D).start()
      val b = new Node("b", D).start()
      a.setPeers(Vector(b))
      b.setPeers(Vector(a))

      a ! MineBlock("hello from a")
      val ok = awaitTrue(10000)(b.currentChain.length == 2 && a.currentChain.length == 2)

      check("peer adopted the mined block", ok)
      check("both sides agree on the tip hash", a.currentChain.last.hash == b.currentChain.last.hash)

      a.stop(); b.stop(); a.join(); b.join()
    }

    // --- Longest-chain rule: a node behind a fork catches up via
    //     RequestChain/ChainResponse, not by being handed state directly ---

    run("a node behind a longer valid fork adopts it wholesale") {
      // `long` mines three blocks entirely on its own -- `short` never sees
      // any of them until the very end, so this is a real fork, not a race.
      val long  = new Node("long", D).start()
      long ! MineBlock("l1")
      awaitTrue(10000)(long.currentChain.length == 2)
      long ! MineBlock("l2")
      awaitTrue(10000)(long.currentChain.length == 3)
      long ! MineBlock("l3")
      awaitTrue(10000)(long.currentChain.length == 4)

      val short = new Node("short", D).start()
      short ! MineBlock("s1")
      awaitTrue(10000)(short.currentChain.length == 2)

      long.setPeers(Vector(short))
      short.setPeers(Vector(long))

      // Announcing long's tip doesn't line up with short's tip (different
      // fork), so short has to ask for the whole chain instead of just
      // splicing one block on.
      short ! NewBlock(long.currentChain.last, long)
      val caughtUp = awaitTrue(10000)(short.currentChain.length == 4)

      check("the shorter fork adopted the longer, valid one", caughtUp)
      check("it adopted the exact chain, not just a matching length", short.currentChain == long.currentChain)

      long.stop(); short.stop(); long.join(); short.join()
    }

    println(s"$total checks, $failed failed")
    if (failed > 0) sys.exit(1)
  }

  private def run(name: String)(body: => Unit): Unit = {
    try body
    catch {
      case e: Throwable =>
        failed += 1
        total += 1
        println(s"FAIL: $name (threw ${e.getClass.getSimpleName}: ${e.getMessage})")
    }
  }
}
