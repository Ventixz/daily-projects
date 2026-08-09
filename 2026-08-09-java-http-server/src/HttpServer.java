import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.file.Path;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * A from-scratch HTTP/1.1 server: raw ServerSocket accept loop, one worker
 * thread per open connection (via a pooled ExecutorService so a burst of
 * clients can't spawn unbounded threads), each worker looping over
 * keep-alive requests on its socket until the client closes it, sends
 * "Connection: close", or an idle-read timeout fires.
 */
public class HttpServer {
    private static final int IDLE_TIMEOUT_MS = 15_000;

    private final int port;
    private final Router router;
    private final ExecutorService pool;
    private volatile ServerSocket serverSocket;

    public HttpServer(int port, Router router, int maxThreads) {
        this.port = port;
        this.router = router;
        this.pool = Executors.newFixedThreadPool(maxThreads);
    }

    public int start() throws IOException {
        serverSocket = new ServerSocket(port);
        int boundPort = serverSocket.getLocalPort();
        Thread acceptThread = new Thread(this::acceptLoop, "http-accept");
        acceptThread.setDaemon(true);
        acceptThread.start();
        return boundPort;
    }

    public void stop() throws IOException {
        if (serverSocket != null) serverSocket.close();
        pool.shutdownNow();
    }

    private void acceptLoop() {
        while (!serverSocket.isClosed()) {
            try {
                Socket socket = serverSocket.accept();
                pool.submit(() -> handleConnection(socket));
            } catch (IOException e) {
                if (serverSocket.isClosed()) return; // expected on stop()
            }
        }
    }

    private void handleConnection(Socket socket) {
        try (socket) {
            socket.setSoTimeout(IDLE_TIMEOUT_MS);
            InputStream in = socket.getInputStream();
            OutputStream out = socket.getOutputStream();

            while (true) {
                HttpRequest request;
                try {
                    request = HttpRequest.parse(in);
                } catch (HttpParseException e) {
                    HttpResponse.text(400, "Bad Request", "Bad Request: " + e.getMessage()).write(out);
                    return;
                } catch (SocketTimeoutException e) {
                    return; // idle keep-alive connection, close quietly
                }
                if (request == null) return; // client closed the connection cleanly

                HttpResponse response = routeSafely(request);
                boolean keepAlive = shouldKeepAlive(request);
                response.headers.putIfAbsent("Connection", keepAlive ? "keep-alive" : "close");
                response.write(out);
                if (!keepAlive) return;
            }
        } catch (IOException e) {
            // client dropped the connection mid-request/response; nothing to respond to
        }
    }

    private HttpResponse routeSafely(HttpRequest request) {
        try {
            return router.route(request);
        } catch (RuntimeException e) {
            return HttpResponse.text(500, "Internal Server Error", "Internal Server Error");
        }
    }

    private static boolean shouldKeepAlive(HttpRequest request) {
        String connection = request.header("connection");
        if (connection != null) return connection.equalsIgnoreCase("keep-alive");
        return request.version.equals("HTTP/1.1"); // 1.1 defaults to persistent, 1.0 does not
    }

    public static void main(String[] args) throws IOException, InterruptedException {
        int port = args.length > 0 ? Integer.parseInt(args[0]) : 8080;
        Path publicRoot = Path.of(args.length > 1 ? args[1] : "public");

        Router router = new Router()
                .get("/api/time", req -> HttpResponse.json(200, "OK",
                        "{\"epochMillis\":" + System.currentTimeMillis() + "}"))
                .get("/api/echo", req -> {
                    String msg = req.query.getOrDefault("msg", "");
                    return HttpResponse.json(200, "OK", "{\"msg\":\"" + jsonEscape(msg) + "\"}");
                })
                .post("/api/echo", req -> {
                    HttpResponse r = HttpResponse.of(200, "OK");
                    r.headers.put("Content-Type", req.headers.getOrDefault("content-type", "application/octet-stream"));
                    r.body = req.body;
                    return r;
                })
                .fallback(new StaticFileHandler(publicRoot));

        HttpServer server = new HttpServer(port, router, 32);
        int boundPort = server.start();
        System.out.println("Listening on http://localhost:" + boundPort + " (serving " + publicRoot.toAbsolutePath() + ")");
        Thread.currentThread().join();
    }

    private static String jsonEscape(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
