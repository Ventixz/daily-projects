package chain

object Blockchain {

  /** A chain is valid if it starts at the canonical genesis block and every
    * link satisfies index/prevHash/hash/proof-of-work.
    */
  def isValid(chain: Vector[Block], difficulty: Int): Boolean =
    chain.nonEmpty && chain.head == Block.genesis &&
      chain.iterator.sliding(2).forall {
        case Seq(prev, curr) => Block.isValidLink(prev, curr, difficulty)
        case _                => true
      }
}
