package chain

import java.security.MessageDigest

final case class Block(
    index: Long,
    timestamp: Long,
    data: String,
    prevHash: String,
    nonce: Long,
    hash: String
)

object Block {

  val GenesisHash: String = "0" * 64

  def hashOf(index: Long, timestamp: Long, data: String, prevHash: String, nonce: Long): String = {
    val payload = s"$index|$timestamp|$data|$prevHash|$nonce"
    val digest  = MessageDigest.getInstance("SHA-256").digest(payload.getBytes("UTF-8"))
    digest.map(b => f"$b%02x").mkString
  }

  val genesis: Block = {
    val h = hashOf(0L, 0L, "genesis", GenesisHash, 0L)
    Block(0L, 0L, "genesis", GenesisHash, 0L, h)
  }

  /** Proof-of-work: brute-force `nonce` until the hash has `difficulty` leading hex zeros. */
  def mine(index: Long, data: String, prevHash: String, difficulty: Int): Block = {
    val prefix    = "0" * difficulty
    val timestamp = System.currentTimeMillis()
    var nonce     = 0L
    var h         = hashOf(index, timestamp, data, prevHash, nonce)
    while (!h.startsWith(prefix)) {
      nonce += 1
      h = hashOf(index, timestamp, data, prevHash, nonce)
    }
    Block(index, timestamp, data, prevHash, nonce, h)
  }

  def isValidLink(prev: Block, curr: Block, difficulty: Int): Boolean = {
    curr.index == prev.index + 1 &&
    curr.prevHash == prev.hash &&
    curr.hash == hashOf(curr.index, curr.timestamp, curr.data, curr.prevHash, curr.nonce) &&
    curr.hash.startsWith("0" * difficulty)
  }
}
