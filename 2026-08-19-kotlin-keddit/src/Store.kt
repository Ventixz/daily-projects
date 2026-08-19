package keddit

enum class Sort { HOT, BEST, TOP, CONTROVERSIAL }

class DuplicateIdException(id: String) : Exception("id already exists: $id")
class UnknownIdException(id: String) : Exception("no post or comment with id: $id")
class UnknownPostException(id: String) : Exception("no post with id: $id")

class Keddit {
    private val posts = LinkedHashMap<String, Post>()
    private val comments = LinkedHashMap<String, Comment>()

    fun addPost(id: String, author: String, title: String, createdAt: Long): Post {
        if (posts.containsKey(id) || comments.containsKey(id)) throw DuplicateIdException(id)
        val post = Post(id, author, title, createdAt)
        posts[id] = post
        return post
    }

    fun addComment(id: String, postId: String, parentId: String?, author: String, text: String, createdAt: Long): Comment {
        if (posts.containsKey(id) || comments.containsKey(id)) throw DuplicateIdException(id)
        if (!posts.containsKey(postId)) throw UnknownPostException(postId)
        if (parentId != null && !comments.containsKey(parentId)) throw UnknownIdException(parentId)
        val comment = Comment(id, postId, parentId, author, text, createdAt)
        comments[id] = comment
        return comment
    }

    fun vote(id: String, up: Boolean) {
        posts[id]?.let { if (up) it.ups++ else it.downs++; return }
        comments[id]?.let { if (up) it.ups++ else it.downs++; return }
        throw UnknownIdException(id)
    }

    fun listPosts(sort: Sort): List<Post> =
        posts.values.sortedByDescending { p -> scoreOf(sort, p.ups, p.downs, p.createdAt) }

    // Recursively builds each post's comments into a tree, sorting every
    // level (not just the top one) by the requested order -- the same
    // shape reddit itself renders: best top-level comments float, and each
    // comment's own replies are independently ranked underneath it.
    fun commentTree(postId: String, sort: Sort): List<CommentNode> {
        if (!posts.containsKey(postId)) throw UnknownPostException(postId)
        val byParent = comments.values.filter { it.postId == postId }.groupBy { it.parentId }
        fun build(parentId: String?): List<CommentNode> {
            val children = byParent[parentId].orEmpty()
            return children
                .sortedByDescending { scoreOf(sort, it.ups, it.downs, it.createdAt) }
                .map { CommentNode(it, build(it.id)) }
        }
        return build(null)
    }

    private fun scoreOf(sort: Sort, ups: Int, downs: Int, createdAt: Long): Double = when (sort) {
        Sort.HOT -> Ranking.hot(ups, downs, createdAt)
        Sort.BEST -> Ranking.confidence(ups, downs)
        Sort.TOP -> Ranking.top(ups, downs).toDouble()
        Sort.CONTROVERSIAL -> Ranking.controversial(ups, downs)
    }
}
