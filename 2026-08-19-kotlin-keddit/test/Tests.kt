package keddit

import kotlin.system.exitProcess

private var failed = 0
private var passed = 0

private fun check(name: String, condition: Boolean) {
    if (condition) {
        passed++
    } else {
        failed++
        println("FAIL: $name")
    }
}

private fun checkClose(name: String, actual: Double, expected: Double, epsilon: Double = 1e-9) {
    check(name, kotlin.math.abs(actual - expected) < epsilon)
}

fun main() {
    testHot()
    testConfidence()
    testControversial()
    testTop()
    testStoreVoting()
    testCommentTree()

    println("$passed passed, $failed failed")
    if (failed > 0) exitProcess(1)
}

private fun testHot() {
    // Same score, later post wins: hot is score-then-recency.
    val older = Ranking.hot(ups = 10, downs = 0, createdAt = 1_000_000L)
    val newer = Ranking.hot(ups = 10, downs = 0, createdAt = 2_000_000L)
    check("hot: newer post outranks older post at equal score", newer > older)

    // Same age, more net upvotes wins.
    val weak = Ranking.hot(ups = 5, downs = 0, createdAt = 1_000_000L)
    val strong = Ranking.hot(ups = 50, downs = 0, createdAt = 1_000_000L)
    check("hot: more net upvotes outranks fewer at equal age", strong > weak)

    // Going from 10 to 100 net votes moves the score by log10(10) = 1,
    // not by 10x -- that's the entire point of the log term.
    val at10 = Ranking.hot(ups = 10, downs = 0, createdAt = 1_000_000L)
    val at100 = Ranking.hot(ups = 100, downs = 0, createdAt = 1_000_000L)
    checkClose("hot: score order is logarithmic in net votes", at100 - at10, 1.0, epsilon = 1e-6)

    // A downvoted post scores below a neutral (0/0) post at the same time.
    val downvoted = Ranking.hot(ups = 0, downs = 5, createdAt = 1_000_000L)
    val neutral = Ranking.hot(ups = 0, downs = 0, createdAt = 1_000_000L)
    check("hot: net-negative score ranks below neutral at equal age", downvoted < neutral)
}

private fun testConfidence() {
    // The classic Wilson-score teaching example: 1 upvote / 0 downvotes is
    // a *worse* bet than 95 upvotes / 5 downvotes, even though the raw
    // ratio (100%) looks better than 95%'s. Small samples get punished.
    val tinySample = Ranking.confidence(ups = 1, downs = 0)
    val bigSample = Ranking.confidence(ups = 95, downs = 5)
    check("confidence: a single upvote loses to a large, mostly-positive sample", bigSample > tinySample)

    // More evidence at the same ratio raises the lower bound.
    val small = Ranking.confidence(ups = 9, downs = 1)
    val large = Ranking.confidence(ups = 90, downs = 10)
    check("confidence: same ratio, more votes raises the lower bound", large > small)

    check("confidence: no votes at all is zero", Ranking.confidence(0, 0) == 0.0)

    val allUp = Ranking.confidence(ups = 20, downs = 0)
    check("confidence: unanimous upvotes stays a valid probability below 1", allUp in 0.0..1.0 && allUp < 1.0)
}

private fun testControversial() {
    // A 50/50 split at high volume beats a lopsided 100:1 crowd.
    val balanced = Ranking.controversial(ups = 50, downs = 50)
    val lopsided = Ranking.controversial(ups = 100, downs = 1)
    check("controversial: even split outranks a lopsided crowd", balanced > lopsided)

    // No opposition on either side means no controversy, by definition.
    check("controversial: all-upvoted is zero", Ranking.controversial(ups = 10, downs = 0) == 0.0)
    check("controversial: all-downvoted is zero", Ranking.controversial(ups = 0, downs = 10) == 0.0)

    // More votes at the same balance is more controversial, not less.
    val fewVotes = Ranking.controversial(ups = 5, downs = 5)
    val manyVotes = Ranking.controversial(ups = 500, downs = 500)
    check("controversial: more votes at the same balance ranks higher", manyVotes > fewVotes)
}

private fun testTop() {
    check("top: is plain net score", Ranking.top(ups = 12, downs = 5) == 7)
    check("top: can go negative", Ranking.top(ups = 1, downs = 9) == -8)
}

private fun testStoreVoting() {
    val keddit = Keddit()
    // 135,000 seconds (37.5h) apart -- exactly 3 "orders of magnitude" of
    // hot's age term (135000 / 45000 = 3), chosen so a plausible vote count
    // (order of 10^3, i.e. ~2000 net upvotes) can actually close the gap.
    keddit.addPost("p1", "alice", "Kotlin is nice", createdAt = 1_000_000L)
    keddit.addPost("p2", "bob", "Old but gold", createdAt = 865_000L)

    // p2 is older with the same starting vote count, so it starts behind
    // p1 under "hot".
    val beforeVotes = keddit.listPosts(Sort.HOT)
    check("store: fresher post starts ahead under hot", beforeVotes.first().id == "p1")

    // Enough upvotes on p2 push it back into the lead.
    repeat(2000) { keddit.vote("p2", up = true) }
    val afterVotes = keddit.listPosts(Sort.HOT)
    check("store: enough votes overcome an age disadvantage under hot", afterVotes.first().id == "p2")

    check("store: top sort reflects the vote pile-up", keddit.listPosts(Sort.TOP).first().id == "p2")

    var threw = false
    try {
        keddit.addPost("p1", "carol", "duplicate id", createdAt = 2L)
    } catch (e: DuplicateIdException) {
        threw = true
    }
    check("store: duplicate post id is rejected", threw)

    var unknownVoteThrew = false
    try {
        keddit.vote("does-not-exist", up = true)
    } catch (e: UnknownIdException) {
        unknownVoteThrew = true
    }
    check("store: voting on an unknown id is rejected", unknownVoteThrew)
}

private fun testCommentTree() {
    val keddit = Keddit()
    keddit.addPost("p1", "alice", "nested comments", createdAt = 1L)
    keddit.addComment("c1", "p1", null, "bob", "weak top-level reply", createdAt = 1L)
    keddit.addComment("c2", "p1", null, "carol", "strong top-level reply", createdAt = 1L)
    keddit.addComment("c3", "p1", "c1", "dave", "reply to bob", createdAt = 1L)
    keddit.addComment("c4", "p1", "c1", "erin", "another reply to bob, more upvoted", createdAt = 1L)

    repeat(20) { keddit.vote("c2", up = true) }
    repeat(5) { keddit.vote("c4", up = true) }

    val tree = keddit.commentTree("p1", Sort.TOP)
    check("comment tree: two top-level roots", tree.size == 2)
    check("comment tree: more-upvoted top-level comment sorts first", tree.first().comment.id == "c2")

    val bobNode = tree.first { it.comment.id == "c1" }
    check("comment tree: bob's replies attach under bob, not at top level", bobNode.children.size == 2)
    check("comment tree: bob's replies are independently sorted by score", bobNode.children.first().comment.id == "c4")

    var unknownParentThrew = false
    try {
        keddit.addComment("c5", "p1", "does-not-exist", "frank", "orphan", createdAt = 1L)
    } catch (e: UnknownIdException) {
        unknownParentThrew = true
    }
    check("comment tree: replying to an unknown parent is rejected", unknownParentThrew)

    var unknownPostThrew = false
    try {
        keddit.addComment("c6", "does-not-exist", null, "frank", "orphan", createdAt = 1L)
    } catch (e: UnknownPostException) {
        unknownPostThrew = true
    }
    check("comment tree: commenting on an unknown post is rejected", unknownPostThrew)
}
