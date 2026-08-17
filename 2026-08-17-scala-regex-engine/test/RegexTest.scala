package regex

// Hand-rolled runner, no test framework: ScalaTest isn't packaged for
// apt on this box and pulling it in would mean sbt resolving jars over
// a network this environment doesn't have to Maven Central. `scalac`
// and `scala` themselves need nothing beyond the classpath, so a plain
// object with a main method is the whole test harness.
object TestRunner {
  var passed = 0
  var failed = 0

  def check(name: String, cond: Boolean): Unit = {
    if (cond) passed += 1
    else { failed += 1; println(s"FAIL: $name") }
  }

  def full(pattern: String, s: String): Boolean =
    Vm.fullMatch(Compiler.compile(Parser.parse(pattern)), s)

  def isParseError(pattern: String): Boolean =
    try { Parser.parse(pattern); false }
    catch { case _: ParseError => true }

  def run(): Unit = {
    // literals & concatenation
    check("literal match", full("abc", "abc"))
    check("literal mismatch", !full("abc", "abd"))
    check("literal prefix alone is not a full match", !full("abc", "ab"))
    check("literal with trailing extra is not a full match", !full("abc", "abcd"))
    check("empty pattern matches empty string", full("", ""))
    check("empty pattern rejects nonempty string", !full("", "a"))

    // any char
    check("dot matches any single char", full("a.c", "abc"))
    check("dot still requires exactly one char", !full("a.c", "ac"))

    // star
    check("star matches zero reps", full("ab*c", "ac"))
    check("star matches many reps", full("ab*c", "abbbbc"))
    check("star alone matches empty string", full("a*", ""))

    // plus
    check("plus rejects zero reps", !full("ab+c", "ac"))
    check("plus matches one rep", full("ab+c", "abc"))
    check("plus matches many reps", full("ab+c", "abbbc"))

    // optional
    check("opt matches zero", full("colou?r", "color"))
    check("opt matches one", full("colou?r", "colour"))
    check("opt rejects two", !full("colou?r", "colouur"))

    // alternation
    check("alt matches left branch", full("cat|dog", "cat"))
    check("alt matches right branch", full("cat|dog", "dog"))
    check("alt rejects neither branch", !full("cat|dog", "cow"))

    // grouping & precedence
    check("group under star, zero reps", full("(ab)*", ""))
    check("group under star, many reps", full("(ab)*", "ababab"))
    check("group under star rejects a partial rep", !full("(ab)*", "aba"))
    check("alt inside parens binds tighter than surrounding concat (left)", full("gr(a|e)y", "gray"))
    check("alt inside parens binds tighter than surrounding concat (right)", full("gr(a|e)y", "grey"))
    check("nested groups", full("((a|b)(c|d))+", "acbd"))
    check("empty group under star is a safe no-op epsilon loop", full("(a)()*(b)", "ab"))

    // escaping
    check("escaped dot is a literal dot", full("""a\.c""", "a.c"))
    check("escaped dot no longer matches any char", !full("""a\.c""", "abc"))
    check("escaped star is a literal star", full("""a\*""", "a*"))

    // find / search (leftmost-longest)
    val cat = Compiler.compile(Parser.parse("cat"))
    check("find locates a match in the middle of the string",
      Vm.find(cat, "concatenate") == Some((3, 6)))
    check("find returns None when the pattern is absent",
      Vm.find(cat, "dogdogdog") == None)

    val aplus = Compiler.compile(Parser.parse("a+"))
    check("find is leftmost and greedy",
      Vm.find(aplus, "  aaa  ") == Some((2, 5)))

    // Thompson simulation explores every alternative simultaneously, so it
    // naturally returns the LONGEST match at the leftmost start position --
    // unlike a backtracking engine, which would stop at whichever
    // alternative in the pattern text it tried first.
    val ambiguous = Compiler.compile(Parser.parse("a|ab"))
    check("thread-based sim prefers the longest match, not the first-written alternative",
      Vm.find(ambiguous, "ab") == Some((0, 2)))

    // parse errors
    check("unmatched open paren is a parse error", isParseError("(ab"))
    check("unmatched close paren is a parse error", isParseError("ab)"))
    check("dangling escape at end of pattern is a parse error", isParseError("ab\\"))
    check("a quantifier with nothing to repeat is a parse error", isParseError("*ab"))

    // Textbook catastrophic-backtracking pattern: (a?){n} a{n} matched
    // against a string of exactly n a's forces a backtracking engine to
    // try every way of assigning the n optional a's before it finds the
    // one assignment (all empty) that works. A thread-based simulation
    // carries all of those assignments as one shared thread list instead
    // of trying them one at a time, so it resolves in O(n) regardless.
    val nOpt = 20
    val nasty = Compiler.compile(Parser.parse(("a?" * nOpt) + ("a" * nOpt)))
    check("catastrophic-backtracking pattern still matches, and does so instantly",
      Vm.fullMatch(nasty, "a" * nOpt))
    check("catastrophic-backtracking pattern correctly rejects one char short",
      !Vm.fullMatch(nasty, "a" * (nOpt - 1)))

    println(s"\n$passed passed, $failed failed")
    if (failed > 0) sys.exit(1)
  }

  def main(args: Array[String]): Unit = run()
}
