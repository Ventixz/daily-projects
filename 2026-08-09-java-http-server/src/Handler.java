@FunctionalInterface
public interface Handler {
    HttpResponse handle(HttpRequest request);
}
