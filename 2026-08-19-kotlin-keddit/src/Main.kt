package keddit

// A tiny line-oriented command language over stdin, so a demo run is just a
// text file you can read top to bottom:
//
//   post <id> <author> <createdAt> <title...>
//   comment <id> <postId> <parentId|-> <author> <createdAt> <text...>
//   vote <id> <up|down>
//   list <hot|best|top|controversial>
//   comments <postId> <hot|best|top|controversial>
//
// `list`/`comments` print immediately, so a script can vote, list, vote
// again, and list again to show ranking move in response to votes.
fun main() {
    val keddit = Keddit()
    generateSequence(::readLine).forEach { rawLine ->
        val line = rawLine.trim()
        if (line.isBlank() || line.startsWith("#")) return@forEach
        try {
            runCommand(keddit, line)
        } catch (e: Exception) {
            println("error: ${e.message}")
        }
    }
}

private fun runCommand(keddit: Keddit, line: String) {
    val parts = line.split(Regex("\\s+"), limit = 5)
    when (parts[0]) {
        "post" -> {
            // post <id> <author> <createdAt> <title...>
            val (id, author, createdAt) = Triple(parts[1], parts[2], parts[3].toLong())
            val title = parts.getOrElse(4) { "" }
            keddit.addPost(id, author, title, createdAt)
        }
        "comment" -> {
            // comment <id> <postId> <parentId|-> <author> <createdAt> <text...>
            val fields = line.split(Regex("\\s+"), limit = 7)
            val (id, postId, parentRaw, author, createdAt) = listOf(
                fields[1], fields[2], fields[3], fields[4], fields[5]
            )
            val parentId = if (parentRaw == "-") null else parentRaw
            val text = fields.getOrElse(6) { "" }
            keddit.addComment(id, postId, parentId, author, text, createdAt.toLong())
        }
        "vote" -> keddit.vote(parts[1], parts[2] == "up")
        "list" -> printPosts(keddit.listPosts(parseSort(parts[1])))
        "comments" -> printComments(keddit.commentTree(parts[1], parseSort(parts[2])))
        else -> println("error: unknown command '${parts[0]}'")
    }
}

private fun parseSort(name: String): Sort = when (name) {
    "hot" -> Sort.HOT
    "best" -> Sort.BEST
    "top" -> Sort.TOP
    "controversial" -> Sort.CONTROVERSIAL
    else -> throw IllegalArgumentException("unknown sort '$name'")
}

private fun printPosts(posts: List<Post>) {
    posts.forEach { println("${it.id}\t+${it.ups}/-${it.downs}\t${it.author}: ${it.title}") }
}

private fun printComments(nodes: List<CommentNode>, depth: Int = 0) {
    for (node in nodes) {
        val indent = "  ".repeat(depth)
        val c = node.comment
        println("$indent${c.id}\t+${c.ups}/-${c.downs}\t${c.author}: ${c.text}")
        printComments(node.children, depth + 1)
    }
}
