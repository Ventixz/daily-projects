import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Serves files out of a root directory. The traversal guard doesn't try to
 * detect ".." in the request string -- it resolves the request against root,
 * normalizes away every ".." and ".", and then checks the *result* is still
 * inside root. That's what makes it immune to encoding tricks or unusual
 * relative-path shapes: the check is on the final destination, not the route.
 */
public class StaticFileHandler implements Handler {
    private final Path root;

    public StaticFileHandler(Path root) {
        this.root = root.toAbsolutePath().normalize();
    }

    @Override
    public HttpResponse handle(HttpRequest request) {
        if (!request.method.equals("GET")) {
            return HttpResponse.text(405, "Method Not Allowed", "Method Not Allowed")
                    .header("Allow", "GET");
        }

        String relative = request.path.equals("/") ? "/index.html" : request.path;
        Path candidate = root.resolve("." + relative).normalize();
        if (!candidate.startsWith(root)) {
            return HttpResponse.text(403, "Forbidden", "Forbidden");
        }
        if (!Files.isRegularFile(candidate)) {
            return HttpResponse.text(404, "Not Found", "Not Found");
        }

        try {
            byte[] bytes = Files.readAllBytes(candidate);
            HttpResponse response = HttpResponse.of(200, "OK");
            response.headers.put("Content-Type", ContentTypes.forPath(candidate.toString()));
            response.body = bytes;
            return response;
        } catch (IOException e) {
            return HttpResponse.text(500, "Internal Server Error", "Internal Server Error");
        }
    }
}
