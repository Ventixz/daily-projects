/**
 * Carries a `return` value up the Java call stack by unwinding through
 * exception handling, rather than threading a "did this block return?" flag
 * through every statement-visiting method. Not an error — callers that use
 * it never print a stack trace, they just unwrap `value`.
 */
class Return extends RuntimeException {
    final Object value;

    Return(Object value) {
        super(null, null, false, false);
        this.value = value;
    }
}
