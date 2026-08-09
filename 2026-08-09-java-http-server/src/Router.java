import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Exact-path routing table keyed first by path, then by method. Keeping method
 * as the inner map (rather than one combined "GET /foo" key) is what makes a
 * path-exists-but-wrong-method request distinguishable from a path that was
 * never registered at all, which is the difference between 405 and 404.
 */
public class Router {
    private final Map<String, Map<String, Handler>> routes = new LinkedHashMap<>();
    private Handler fallback;

    public Router on(String method, String path, Handler handler) {
        routes.computeIfAbsent(path, p -> new LinkedHashMap<>()).put(method.toUpperCase(), handler);
        return this;
    }

    public Router get(String path, Handler handler) { return on("GET", path, handler); }
    public Router post(String path, Handler handler) { return on("POST", path, handler); }

    /** Handler invoked for any path with no registered route at all (e.g. static files, then 404). */
    public Router fallback(Handler handler) {
        this.fallback = handler;
        return this;
    }

    public HttpResponse route(HttpRequest request) {
        Map<String, Handler> byMethod = routes.get(request.path);
        if (byMethod == null) {
            return fallback != null ? fallback.handle(request) : HttpResponse.text(404, "Not Found", "Not Found");
        }
        Handler handler = byMethod.get(request.method.toUpperCase());
        if (handler == null) {
            String allow = String.join(", ", byMethod.keySet());
            return HttpResponse.text(405, "Method Not Allowed", "Method Not Allowed")
                    .header("Allow", allow);
        }
        return handler.handle(request);
    }
}
