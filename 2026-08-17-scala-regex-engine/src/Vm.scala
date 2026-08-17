package regex

import scala.collection.mutable.ArrayBuffer

// Pike's VM: simulate every live NFA thread in lockstep, one input
// character at a time, instead of backtracking through one thread at a
// time. Each thread is just a program counter; a step advances every
// thread that can consume the current character and drops the rest. This
// is what keeps matching O(n * m) (n = input length, m = program size)
// no matter how the pattern is written -- there is no path through this
// code that revisits the same (pc, input position) pair twice.
object Vm {

  // Epsilon-closure of `pc`, added into `list` in the order threads should
  // be tried (first alternative first), skipping any pc already added
  // during this step. The `gen` stamp array is what makes that skip O(1)
  // instead of needing to clear a visited set between every step.
  private def addThread(
      prog: Array[Inst],
      gen: Array[Int],
      genId: Int,
      list: ArrayBuffer[Int],
      pc: Int
  ): Unit = {
    if (gen(pc) == genId) return
    gen(pc) = genId
    prog(pc) match {
      case IJmp(x)       => addThread(prog, gen, genId, list, x)
      case ISplit(x, y) =>
        addThread(prog, gen, genId, list, x)
        addThread(prog, gen, genId, list, y)
      case _ => list += pc
    }
  }

  // Length of the longest match anchored at `start` (i.e. beginning
  // exactly at `input(start)`, end unconstrained), or -1 if `start`
  // cannot begin a match at all. Because every thread advances together,
  // a later, longer match for the same start naturally overwrites an
  // earlier, shorter one -- this is what gives leftmost-longest results
  // "for free", where a backtracking engine would instead return
  // whichever alternative it tried first.
  def matchFrom(prog: Array[Inst], input: String, start: Int): Int = {
    val gen = new Array[Int](prog.length)
    var genId = 0
    var clist = ArrayBuffer[Int]()
    genId += 1
    addThread(prog, gen, genId, clist, 0)

    var best = -1
    var i = start
    while (true) {
      if (clist.exists(pc => prog(pc) == IMatch)) best = i - start

      if (i >= input.length || clist.isEmpty) return best

      val c = input(i)
      val nlist = ArrayBuffer[Int]()
      genId += 1
      for (pc <- clist) {
        prog(pc) match {
          case IChar(ch) if ch == c => addThread(prog, gen, genId, nlist, pc + 1)
          case IAny                 => addThread(prog, gen, genId, nlist, pc + 1)
          case _                    => ()
        }
      }
      clist = nlist
      i += 1
    }
    best // unreachable, satisfies the type checker
  }

  def fullMatch(prog: Array[Inst], input: String): Boolean =
    matchFrom(prog, input, 0) == input.length

  // Leftmost-longest search: the first start position that can match at
  // all, extended as far as it will go. O(n) attempts of an O(m) sim
  // each, so O(n*m) overall -- see LEARNING.md for why this doesn't fold
  // the "try every start" loop into the VM itself the way a real grep
  // implementation would.
  def find(prog: Array[Inst], input: String): Option[(Int, Int)] = {
    var start = 0
    while (start <= input.length) {
      val len = matchFrom(prog, input, start)
      if (len >= 0) return Some((start, start + len))
      start += 1
    }
    None
  }
}
