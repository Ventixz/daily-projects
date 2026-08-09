import java.io.IOException;

/** A malformed request. Distinct from a plain IOException (socket died) so the
 *  connection loop can answer with 400 Bad Request instead of just dropping the socket. */
public class HttpParseException extends IOException {
    public HttpParseException(String message) {
        super(message);
    }
}
