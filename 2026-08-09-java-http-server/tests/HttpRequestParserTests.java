import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;


public class HttpRequestParserTests {

    private static InputStream stream(String raw) {
        return new ByteArrayInputStream(raw.getBytes(StandardCharsets.UTF_8));
    }

    static void run() throws IOException {
        testBasicGet();
        testHeaderCaseInsensitiveAndFolding();
        testQueryStringDecoding();
        testPathPercentDecodingDoesNotTouchPlus();
        testPostBodyReadsExactContentLength();
        testMissingContentLengthMeansEmptyBody();
        testMalformedRequestLineThrows();
        testMalformedHeaderThrows();
        testBadContentLengthThrows();
        testCleanEofBetweenRequestsReturnsNull();
        testTruncatedBodyThrows();
    }

    static void testBasicGet() throws IOException {
        HttpRequest req = HttpRequest.parse(stream(
                "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n"));
        TestHelper.assertEquals("GET", req.method, "method");
        TestHelper.assertEquals("/hello", req.path, "path");
        TestHelper.assertEquals("HTTP/1.1", req.version, "version");
        TestHelper.assertEquals("example.com", req.header("host"), "host header");
        TestHelper.assertEquals(0, req.body.length, "no body for a headerless GET");
    }

    static void testHeaderCaseInsensitiveAndFolding() throws IOException {
        HttpRequest req = HttpRequest.parse(stream(
                "GET / HTTP/1.1\r\nX-Thing: a\r\nx-thing: b\r\n\r\n"));
        TestHelper.assertEquals("a, b", req.header("X-THING"), "repeated headers merge in order, lookup is case-insensitive");
    }

    static void testQueryStringDecoding() throws IOException {
        HttpRequest req = HttpRequest.parse(stream(
                "GET /search?q=hello+world&tag=a%26b HTTP/1.1\r\n\r\n"));
        TestHelper.assertEquals("/search", req.path, "path excludes query string");
        TestHelper.assertEquals("hello world", req.query.get("q"), "'+' decodes to space in query values");
        TestHelper.assertEquals("a&b", req.query.get("tag"), "%26 decodes to a literal '&' in query values");
    }

    static void testPathPercentDecodingDoesNotTouchPlus() throws IOException {
        HttpRequest req = HttpRequest.parse(stream(
                "GET /a+b/%41 HTTP/1.1\r\n\r\n"));
        TestHelper.assertEquals("/a+b/A", req.path, "path decodes %XX but leaves '+' literal, unlike a query string");
    }

    static void testPostBodyReadsExactContentLength() throws IOException {
        String body = "{\"x\":1}";
        HttpRequest req = HttpRequest.parse(stream(
                "POST /api HTTP/1.1\r\nContent-Length: " + body.length() + "\r\n\r\n" + body + "TRAILING_GARBAGE"));
        TestHelper.assertEquals(body, new String(req.body, StandardCharsets.UTF_8), "body is read to exactly Content-Length bytes");
    }

    static void testMissingContentLengthMeansEmptyBody() throws IOException {
        HttpRequest req = HttpRequest.parse(stream("GET / HTTP/1.1\r\n\r\n"));
        TestHelper.assertEquals(0, req.body.length, "no Content-Length header means zero-length body");
    }

    static void testMalformedRequestLineThrows() {
        boolean threw = false;
        try {
            HttpRequest.parse(stream("NOT A REQUEST LINE AT ALL\r\n\r\n"));
        } catch (HttpParseException e) {
            threw = true;
        } catch (IOException e) {
            // unexpected type
        }
        TestHelper.assertTrue(threw, "a request line with the wrong number of parts throws HttpParseException");
    }

    static void testMalformedHeaderThrows() {
        boolean threw = false;
        try {
            HttpRequest.parse(stream("GET / HTTP/1.1\r\nNoColonHere\r\n\r\n"));
        } catch (HttpParseException e) {
            threw = true;
        } catch (IOException e) {
            // unexpected type
        }
        TestHelper.assertTrue(threw, "a header line with no colon throws HttpParseException");
    }

    static void testBadContentLengthThrows() {
        boolean threw = false;
        try {
            HttpRequest.parse(stream("POST / HTTP/1.1\r\nContent-Length: not-a-number\r\n\r\n"));
        } catch (HttpParseException e) {
            threw = true;
        } catch (IOException e) {
            // unexpected type
        }
        TestHelper.assertTrue(threw, "a non-numeric Content-Length throws HttpParseException");
    }

    static void testCleanEofBetweenRequestsReturnsNull() throws IOException {
        HttpRequest req = HttpRequest.parse(stream(""));
        TestHelper.assertNull(req, "EOF before any bytes arrive (the normal end of a keep-alive connection) returns null, not an exception");
    }

    static void testTruncatedBodyThrows() {
        boolean threw = false;
        try {
            HttpRequest.parse(stream("POST / HTTP/1.1\r\nContent-Length: 100\r\n\r\nshort"));
        } catch (HttpParseException e) {
            threw = true;
        } catch (IOException e) {
            // unexpected type
        }
        TestHelper.assertTrue(threw, "a body shorter than its declared Content-Length throws rather than returning a partial body");
    }
}
