package keddit

data class Post(
    val id: String,
    val author: String,
    val title: String,
    val createdAt: Long,
    var ups: Int = 1,
    var downs: Int = 0
)

data class Comment(
    val id: String,
    val postId: String,
    val parentId: String?,
    val author: String,
    val text: String,
    val createdAt: Long,
    var ups: Int = 1,
    var downs: Int = 0
)

data class CommentNode(val comment: Comment, val children: List<CommentNode>)
