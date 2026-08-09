import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

/**
 * A parsed HTTP request. Parsing reads directly off the socket's InputStream
 * (not a BufferedReader) so the body -- which may be binary -- is read as raw
 * bytes rather than decoded/re-encoded through a Reader's charset.
 */
public class HttpRequest {
    private static final int MAX_START_LINE_LENGTH = 8192;
    private static final int MAX_HEADER_COUNT = 100;

    public final String method;
    public final String path;
    public final Map<String, String> query;
    public final String version;
    public final Map<String, String> headers; // case-insensitive keys (stored lowercase)
    public final byte[] body;

    private HttpRequest(String method, String path, Map<String, String> query,
                         String version, Map<String, String> headers, byte[] body) {
        this.method = method;
        this.path = path;
        this.query = query;
        this.version = version;
        this.headers = headers;
        this.body = body;
    }

    public String header(String name) {
        return headers.get(name.toLowerCase());
    }

    /**
     * Returns null if the stream is closed before a single byte of a new
     * request arrives -- the expected way a keep-alive connection ends.
     * Throws HttpParseException for anything that starts a request but is
     * malformed partway through.
     */
    public static HttpRequest parse(InputStream in) throws IOException, HttpParseException {
        String startLine = readLine(in, MAX_START_LINE_LENGTH);
        if (startLine == null) return null; // clean EOF between requests
        if (startLine.isEmpty()) {
            // Tolerate a stray leading CRLF some clients send between pipelined requests.
            startLine = readLine(in, MAX_START_LINE_LENGTH);
            if (startLine == null) return null;
        }

        String[] parts = startLine.split(" ");
        if (parts.length != 3) {
            throw new HttpParseException("malformed request line: " + startLine);
        }
        String method = parts[0];
        String target = parts[1];
        String version = parts[2];
        if (!version.equals("HTTP/1.1") && !version.equals("HTTP/1.0")) {
            throw new HttpParseException("unsupported version: " + version);
        }

        String rawPath;
        Map<String, String> query = new LinkedHashMap<>();
        int qIdx = target.indexOf('?');
        if (qIdx >= 0) {
            rawPath = target.substring(0, qIdx);
            parseQuery(target.substring(qIdx + 1), query);
        } else {
            rawPath = target;
        }
        if (!rawPath.startsWith("/")) {
            throw new HttpParseException("malformed request target: " + target);
        }
        String path = percentDecodePath(rawPath);

        Map<String, String> headers = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        int headerCount = 0;
        while (true) {
            String line = readLine(in, MAX_START_LINE_LENGTH);
            if (line == null) throw new HttpParseException("connection closed mid-headers");
            if (line.isEmpty()) break;
            if (++headerCount > MAX_HEADER_COUNT) {
                throw new HttpParseException("too many headers");
            }
            int colon = line.indexOf(':');
            if (colon <= 0) throw new HttpParseException("malformed header: " + line);
            String name = line.substring(0, colon).trim();
            String value = line.substring(colon + 1).trim();
            headers.merge(name.toLowerCase(), value, (a, b) -> a + ", " + b);
        }

        byte[] body = new byte[0];
        String contentLength = headers.get("content-length");
        if (contentLength != null) {
            int len;
            try {
                len = Integer.parseInt(contentLength.trim());
            } catch (NumberFormatException e) {
                throw new HttpParseException("bad content-length: " + contentLength);
            }
            if (len < 0) throw new HttpParseException("negative content-length");
            body = readExactly(in, len);
        }

        return new HttpRequest(method, path, query, version, headers, body);
    }

    private static void parseQuery(String raw, Map<String, String> out) {
        if (raw.isEmpty()) return;
        for (String pair : raw.split("&")) {
            if (pair.isEmpty()) continue;
            int eq = pair.indexOf('=');
            String key = eq >= 0 ? pair.substring(0, eq) : pair;
            String value = eq >= 0 ? pair.substring(eq + 1) : "";
            out.put(urlDecode(key), urlDecode(value));
        }
    }

    /** Percent-decodes a path segment. Unlike query decoding, '+' is a literal plus here,
     *  not a space -- that substitution is specific to application/x-www-form-urlencoded. */
    private static String percentDecodePath(String s) {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream(s.length());
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '%' && i + 2 < s.length()) {
                try {
                    int value = Integer.parseInt(s.substring(i + 1, i + 3), 16);
                    out.write(value);
                    i += 2;
                    continue;
                } catch (NumberFormatException ignored) {
                    // fall through: not valid hex, treat '%' literally
                }
            }
            out.write(c);
        }
        return out.toString(StandardCharsets.UTF_8);
    }

    private static String urlDecode(String s) {
        try {
            return java.net.URLDecoder.decode(s, StandardCharsets.UTF_8);
        } catch (IllegalArgumentException e) {
            return s;
        }
    }

    /** Reads a single CRLF- or LF-terminated line as Latin-1 (HTTP header bytes are ASCII-safe). */
    private static String readLine(InputStream in, int maxLength) throws IOException {
        StringBuilder sb = new StringBuilder();
        int prev = -1;
        int b;
        boolean any = false;
        while ((b = in.read()) != -1) {
            any = true;
            if (b == '\n') {
                if (prev == '\r') sb.setLength(sb.length() - 1);
                return sb.toString();
            }
            sb.append((char) b);
            if (sb.length() > maxLength) throw new HttpParseException("line too long");
            prev = b;
        }
        return any ? sb.toString() : null;
    }

    private static byte[] readExactly(InputStream in, int len) throws IOException {
        byte[] buf = new byte[len];
        int off = 0;
        while (off < len) {
            int n = in.read(buf, off, len - off);
            if (n == -1) throw new HttpParseException("connection closed mid-body");
            off += n;
        }
        return buf;
    }
}
