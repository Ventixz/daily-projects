package chain

// Messages a Node actor understands.
final case class MineBlock(data: String)
final case class NewBlock(block: Block, from: Node)
final case class RequestChain(from: Node)
final case class ChainResponse(chain: Vector[Block], from: Node)

/**
 * A blockchain peer. Each `Node` is its own actor with its own copy of the
 * chain; there is no shared mutable ledger anywhere in the program. Peers
 * only ever learn about each other's state through messages, and disputes
 * are resolved by the longest-valid-chain rule -- the same rule every
 * simplified blockchain tutorial reaches for, here made unavoidable because
 * two `Node`s literally cannot see each other's memory.
 */
class Node(val name: String, difficulty: Int) extends Actor {

  @volatile private var chain: Vector[Block] = Vector(Block.genesis)
  private var peers: Vector[Node]            = Vector.empty

  /** Read-only snapshot for tests/observers. Safe: `chain` is `@volatile`
    * and `Vector` is immutable, so a reader never sees a half-built chain.
    */
  def currentChain: Vector[Block] = chain

  def setPeers(ps: Vector[Node]): Unit = peers = ps

  protected def receive: PartialFunction[Any, Unit] = {

    case MineBlock(data) =>
      val next = Block.mine(chain.last.index + 1, data, chain.last.hash, difficulty)
      chain = chain :+ next
      peers.foreach(_ ! NewBlock(next, this))

    case NewBlock(block, from) =>
      if (block.index == chain.length && Block.isValidLink(chain.last, block, difficulty)) {
        // Extends our tip directly: adopt it and relay to everyone else.
        chain = chain :+ block
        peers.filterNot(_ eq from).foreach(_ ! NewBlock(block, this))
      } else if (block.index >= chain.length) {
        // We're behind (or this block doesn't chain onto our tip): ask for
        // the sender's whole history instead of guessing at the gap.
        from ! RequestChain(this)
      }
    // block.index < chain.length: we've already moved past this, ignore it.

    case RequestChain(from) =>
      from ! ChainResponse(chain, this)

    case ChainResponse(theirChain, _) =>
      if (theirChain.length > chain.length && Blockchain.isValid(theirChain, difficulty)) {
        chain = theirChain
      }
  }
}
