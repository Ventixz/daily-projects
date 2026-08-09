import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;


public class RouterTests {

    private static HttpRequest request(String method, String path) throws IOException {
        return HttpRequest.parse(new ByteArrayInputStream(
                (method + " " + path + " HTTP/1.1\r\n\r\n").getBytes(StandardCharsets.UTF_8)));
    }

    static void run() throws IOException {
        testExactRouteMatches();
        testWrongMethodOnKnownPathIs405WithAllowHeader();
        testUnknownPathWithNoFallbackIs404();
        testUnknownPathFallsThroughToFallbackHandler();
    }

    static void testExactRouteMatches() throws IOException {
        Router router = new Router().get("/hi", req -> HttpResponse.text(200, "OK", "hi there"));
        HttpResponse res = router.route(request("GET", "/hi"));
        TestHelper.assertEquals(200, res.status, "registered GET route matches");
        TestHelper.assertEquals("hi there", new String(res.body, StandardCharsets.UTF_8), "handler's body is returned verbatim");
    }

    static void testWrongMethodOnKnownPathIs405WithAllowHeader() throws IOException {
        Router router = new Router().get("/hi", req -> HttpResponse.text(200, "OK", "hi"));
        HttpResponse res = router.route(request("POST", "/hi"));
        TestHelper.assertEquals(405, res.status, "known path, unregistered method -> 405, not 404");
        TestHelper.assertEquals("GET", res.headers.get("Allow"), "Allow header lists the methods that ARE registered for this path");
    }

    static void testUnknownPathWithNoFallbackIs404() throws IOException {
        Router router = new Router().get("/hi", req -> HttpResponse.text(200, "OK", "hi"));
        HttpResponse res = router.route(request("GET", "/nowhere"));
        TestHelper.assertEquals(404, res.status, "a path with no route at all and no fallback is 404");
    }

    static void testUnknownPathFallsThroughToFallbackHandler() throws IOException {
        Router router = new Router()
                .get("/hi", req -> HttpResponse.text(200, "OK", "hi"))
                .fallback(req -> HttpResponse.text(418, "I'm a teapot", "caught by fallback: " + req.path));
        HttpResponse res = router.route(request("GET", "/anything"));
        TestHelper.assertEquals(418, res.status, "unregistered path is handed to the fallback handler instead of an automatic 404");
        TestHelper.assertEquals("caught by fallback: /anything", new String(res.body, StandardCharsets.UTF_8), "fallback sees the real request");
    }
}
