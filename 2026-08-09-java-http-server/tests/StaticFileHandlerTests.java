import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;


public class StaticFileHandlerTests {

    private static HttpRequest getRequest(String path) throws IOException {
        return HttpRequest.parse(new ByteArrayInputStream(
                ("GET " + path + " HTTP/1.1\r\n\r\n").getBytes(StandardCharsets.UTF_8)));
    }

    static void run() throws IOException {
        Path sandbox = Files.createTempDirectory("http-server-test");
        Path webRoot = sandbox.resolve("public");
        Files.createDirectories(webRoot);
        Files.writeString(webRoot.resolve("index.html"), "<h1>home</h1>");
        Files.writeString(webRoot.resolve("style.css"), "body{}");
        // A file OUTSIDE webRoot, sibling to it, that a traversal attempt might reach.
        Files.writeString(sandbox.resolve("secret.txt"), "should never be served");

        StaticFileHandler handler = new StaticFileHandler(webRoot);

        testServesIndexAtSlash(handler);
        testContentTypeByExtension(handler);
        testMissingFileIs404(handler);
        testTraversalOutsideRootIs403(handler, sandbox);
        testNonGetMethodIs405(handler);
    }

    static void testServesIndexAtSlash(StaticFileHandler handler) throws IOException {
        HttpResponse res = handler.handle(getRequest("/"));
        TestHelper.assertEquals(200, res.status, "/ serves index.html");
        TestHelper.assertEquals("<h1>home</h1>", new String(res.body, StandardCharsets.UTF_8), "index.html content");
    }

    static void testContentTypeByExtension(StaticFileHandler handler) throws IOException {
        HttpResponse res = handler.handle(getRequest("/style.css"));
        TestHelper.assertEquals(200, res.status, "/style.css found");
        TestHelper.assertEquals("text/css; charset=utf-8", res.headers.get("Content-Type"), "extension maps to the right MIME type");
    }

    static void testMissingFileIs404(StaticFileHandler handler) throws IOException {
        HttpResponse res = handler.handle(getRequest("/nope.html"));
        TestHelper.assertEquals(404, res.status, "a file that doesn't exist under root is 404");
    }

    static void testTraversalOutsideRootIs403(StaticFileHandler handler, Path sandbox) throws IOException {
        HttpResponse res = handler.handle(getRequest("/../secret.txt"));
        TestHelper.assertEquals(403, res.status, "resolving '..' to a path outside root is 403, not the sibling file's contents");
        // Belt and suspenders: prove the file really exists where a naive implementation would have found it.
        TestHelper.assertEquals(true, Files.exists(sandbox.resolve("secret.txt")), "the file being guarded against actually exists");
    }

    static void testNonGetMethodIs405(StaticFileHandler handler) throws IOException {
        HttpRequest post = HttpRequest.parse(new ByteArrayInputStream(
                "POST /index.html HTTP/1.1\r\n\r\n".getBytes(StandardCharsets.UTF_8)));
        HttpResponse res = handler.handle(post);
        TestHelper.assertEquals(405, res.status, "static file handler only serves GET");
    }
}
