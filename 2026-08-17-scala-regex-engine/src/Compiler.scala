package regex

sealed trait Inst
case class IChar(c: Char) extends Inst
case object IAny extends Inst
case class ISplit(x: Int, y: Int) extends Inst
case class IJmp(x: Int) extends Inst
case object IMatch extends Inst

// Thompson construction, in the style of Russ Cox's "Regular Expression
// Matching Can Be Simple And Fast": compile the AST straight to a flat
// instruction array instead of building an explicit graph of NFA-state
// objects. Split/Jmp targets are plain array indices, patched in after
// each branch is emitted because the size of a branch isn't known until
// it's been emitted.
object Compiler {
  def compile(node: Node): Array[Inst] = {
    val prog = scala.collection.mutable.ArrayBuffer[Inst]()

    def emit(n: Node): Unit = n match {
      case Epsilon => ()
      case Lit(c) => prog += IChar(c)
      case AnyChar => prog += IAny

      case Concat(a, b) =>
        emit(a)
        emit(b)

      case Alt(a, b) =>
        val splitPc = prog.length
        prog += ISplit(0, 0) // placeholder, patched below
        val pc1 = prog.length
        emit(a)
        val jmpPc = prog.length
        prog += IJmp(0) // placeholder, patched below
        val pc2 = prog.length
        emit(b)
        val pc3 = prog.length
        prog(splitPc) = ISplit(pc1, pc2)
        prog(jmpPc) = IJmp(pc3)

      case Star(a) =>
        val splitPc = prog.length
        prog += ISplit(0, 0)
        val pc1 = prog.length
        emit(a)
        prog += IJmp(splitPc)
        val pc2 = prog.length
        prog(splitPc) = ISplit(pc1, pc2)

      case Plus(a) =>
        val pc1 = prog.length
        emit(a)
        val splitPc = prog.length
        prog += ISplit(pc1, splitPc + 1)

      case Opt(a) =>
        val splitPc = prog.length
        prog += ISplit(0, 0)
        val pc1 = prog.length
        emit(a)
        val pc2 = prog.length
        prog(splitPc) = ISplit(pc1, pc2)
    }

    emit(node)
    prog += IMatch
    prog.toArray
  }
}
