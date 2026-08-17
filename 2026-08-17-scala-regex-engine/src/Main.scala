package regex

object Main {
  def main(args: Array[String]): Unit = {
    if (args.isEmpty) {
      System.err.println("usage: Main <pattern> [string...]")
      System.err.println("       echo text | Main <pattern>   (search each stdin line)")
      sys.exit(2)
    }

    val pattern = args(0)
    val node =
      try Parser.parse(pattern)
      catch {
        case e: ParseError =>
          System.err.println(s"parse error: ${e.getMessage}")
          sys.exit(2)
      }
    val prog = Compiler.compile(node)

    val inputs =
      if (args.length > 1) args.drop(1).toList
      else scala.io.Source.stdin.getLines().toList

    for (line <- inputs) {
      val isFull = Vm.fullMatch(prog, line)
      Vm.find(prog, line) match {
        case Some((s, e)) =>
          val tag = if (isFull) "full" else "find"
          val matched = line.substring(s, e)
          println(tag + " [" + s + "," + e + ") \"" + matched + "\"  <- " + line)
        case None =>
          println(s"no match  <- $line")
      }
    }
  }
}
