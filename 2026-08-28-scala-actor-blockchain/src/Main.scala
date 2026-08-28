package chain

object Main {

  val Difficulty = 4

  /** Poll a condition until it holds or the timeout elapses. */
  def awaitTrue(timeoutMs: Long)(cond: => Boolean): Boolean = {
    val deadline = System.currentTimeMillis() + timeoutMs
    while (System.currentTimeMillis() < deadline && !cond) Thread.sleep(20)
    cond
  }

  def main(args: Array[String]): Unit = {
    val alice = new Node("alice", Difficulty)
    val bob   = new Node("bob", Difficulty)
    val carol = new Node("carol", Difficulty)
    val nodes = Vector(alice, bob, carol)

    nodes.foreach(n => n.setPeers(nodes.filterNot(_ eq n)))
    nodes.foreach(_.start())

    println(s"Mining with difficulty $Difficulty (hashes must start with ${"0" * Difficulty})...")

    // Mine one block at a time, from a different node each round, waiting for
    // the whole mesh to catch up before the next round starts. This keeps the
    // demo to a single, ever-growing chain -- no two nodes race to extend the
    // same tip, so there's no fork here to resolve (that's what the test
    // suite's longest-chain scenario is for).
    val miners = Vector(alice, bob, carol)
    miners.zipWithIndex.foreach { case (miner, i) =>
      miner ! MineBlock(s"block #${i + 1} from ${miner.name}")
      awaitTrue(15000)(nodes.forall(_.currentChain.length == i + 2))
    }

    val converged = {
      val lengths = nodes.map(_.currentChain.length)
      lengths.forall(_ == lengths.head) && lengths.head == 4 // genesis + 3 mined blocks
    }

    nodes.foreach { n =>
      val c = n.currentChain
      println(s"${n.name}: ${c.length} blocks, valid=${Blockchain.isValid(c, Difficulty)}, tip=${c.last.hash.take(12)}...")
    }
    println(if (converged) "All nodes converged on the same chain." else "Nodes did not converge in time.")

    nodes.foreach(_.stop())
    nodes.foreach(_.join())
  }
}
