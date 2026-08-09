import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

public class HttpResponse {
    public int status = 200;
    public String reason = "OK";
    public final Map<String, String> headers = new LinkedHashMap<>();
    public byte[] body = new byte[0];

    public static HttpResponse of(int status, String reason) {
        HttpResponse r = new HttpResponse();
        r.status = status;
        r.reason = reason;
        return r;
    }

    public static HttpResponse text(int status, String reason, String body) {
        HttpResponse r = of(status, reason);
        r.body = body.getBytes(StandardCharsets.UTF_8);
        r.headers.put("Content-Type", "text/plain; charset=utf-8");
        return r;
    }

    public static HttpResponse json(int status, String reason, String jsonBody) {
        HttpResponse r = of(status, reason);
        r.body = jsonBody.getBytes(StandardCharsets.UTF_8);
        r.headers.put("Content-Type", "application/json; charset=utf-8");
        return r;
    }

    public HttpResponse header(String name, String value) {
        headers.put(name, value);
        return this;
    }

    /** Writes the full status line, headers, and body. Content-Length is always
     *  set here from the actual body array so callers can never send a mismatched one. */
    void write(OutputStream out) throws IOException {
        StringBuilder head = new StringBuilder();
        head.append("HTTP/1.1 ").append(status).append(' ').append(reason).append("\r\n");
        for (Map.Entry<String, String> h : headers.entrySet()) {
            if (h.getKey().equalsIgnoreCase("content-length")) continue;
            head.append(h.getKey()).append(": ").append(h.getValue()).append("\r\n");
        }
        head.append("Content-Length: ").append(body.length).append("\r\n");
        head.append("\r\n");
        out.write(head.toString().getBytes(StandardCharsets.US_ASCII));
        out.write(body);
        out.flush();
    }
}
