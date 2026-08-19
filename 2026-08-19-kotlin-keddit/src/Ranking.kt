package keddit

import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Reddit's own three public sort orders, reimplemented from the published
 * formulas (see LEARNING.md for where each one comes from). All three take
 * plain (ups, downs, createdAt) rather than a Post/Comment so the same code
 * ranks both.
 */
object Ranking {

    // Seconds between the Unix epoch and 2005-12-08T07:46:43Z, the moment
    // reddit's own "hot" formula treats as t=0. Kept as a named constant,
    // not because the exact date matters (any fixed origin gives the same
    // *ordering*), but because a magic number here would hide that this is
    // reddit's real epoch, not an arbitrary one.
    private const val REDDIT_EPOCH = 1134028003L

    fun hot(ups: Int, downs: Int, createdAt: Long): Double {
        val score = (ups - downs).toDouble()
        val order = log10(max(abs(score), 1.0))
        val sign = when {
            score > 0 -> 1.0
            score < 0 -> -1.0
            else -> 0.0
        }
        val seconds = createdAt - REDDIT_EPOCH
        return sign * order + seconds / 45000.0
    }

    fun top(ups: Int, downs: Int): Int = ups - downs

    // Wilson score lower bound (95% confidence) on the true upvote
    // proportion, treating each vote as a Bernoulli trial. This is what
    // reddit calls "best": it punishes small sample sizes instead of
    // trusting a raw ups/(ups+downs) ratio.
    fun confidence(ups: Int, downs: Int): Double {
        val n = (ups + downs).toDouble()
        if (n == 0.0) return 0.0
        val z = 1.959963985 // 95% two-tailed normal z-score
        val p = ups / n
        val z2 = z * z
        val left = p + z2 / (2 * n)
        val right = z * sqrt((p * (1 - p) + z2 / (4 * n)) / n)
        val under = 1 + z2 / n
        return (left - right) / under
    }

    // High volume of votes that are nearly evenly split beats either a
    // lopsided crowd or a quiet one. Ties to zero on either side: you can't
    // be controversial without opposition.
    fun controversial(ups: Int, downs: Int): Double {
        if (ups <= 0 || downs <= 0) return 0.0
        val magnitude = (ups + downs).toDouble()
        val balance = if (ups > downs) downs.toDouble() / ups else ups.toDouble() / downs
        return magnitude.pow(balance)
    }
}
