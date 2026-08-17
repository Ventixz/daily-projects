package regex

sealed trait Node
case class Lit(c: Char) extends Node
case object AnyChar extends Node
case object Epsilon extends Node
case class Concat(a: Node, b: Node) extends Node
case class Alt(a: Node, b: Node) extends Node
case class Star(a: Node) extends Node
case class Plus(a: Node) extends Node
case class Opt(a: Node) extends Node
