package regex

class ParseError(msg: String) extends Exception(msg)

// expr    := term ('|' term)*
// term    := factor*
// factor  := atom ('*' | '+' | '?')*
// atom    := '(' expr ')' | '.' | '\' any-char | any-other-char
object Parser {
  def parse(pattern: String): Node = {
    val p = new Parser(pattern)
    val node = p.parseAlt()
    if (!p.atEnd) throw new ParseError(s"unexpected '${p.peek}' at position ${p.pos}")
    node
  }
}

private class Parser(s: String) {
  var pos = 0
  def atEnd: Boolean = pos >= s.length
  def peek: Char = s(pos)
  def advance(): Char = { val c = s(pos); pos += 1; c }
  def expect(c: Char): Unit =
    if (atEnd || peek != c) throw new ParseError(s"expected '$c' at position $pos")
    else advance()

  def parseAlt(): Node = {
    var node = parseConcat()
    while (!atEnd && peek == '|') {
      advance()
      node = Alt(node, parseConcat())
    }
    node
  }

  def parseConcat(): Node = {
    var node: Node = Epsilon
    var first = true
    while (!atEnd && peek != '|' && peek != ')') {
      val f = parseFactor()
      node = if (first) f else Concat(node, f)
      first = false
    }
    node
  }

  def parseFactor(): Node = {
    var node = parseAtom()
    while (!atEnd && (peek == '*' || peek == '+' || peek == '?')) {
      node = advance() match {
        case '*' => Star(node)
        case '+' => Plus(node)
        case '?' => Opt(node)
      }
    }
    node
  }

  def parseAtom(): Node = {
    if (atEnd) throw new ParseError("unexpected end of pattern")
    peek match {
      case '*' | '+' | '?' =>
        throw new ParseError(s"nothing to repeat at position $pos")
      case _ =>
        advance() match {
          case '(' =>
            val node = parseAlt()
            expect(')')
            node
          case '.' => AnyChar
          case '\\' =>
            if (atEnd) throw new ParseError("dangling escape at end of pattern")
            Lit(advance())
          case c => Lit(c)
        }
    }
  }
}
