import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;


/**
 * Drives a real HttpServer instance over a real socket -- the thing every
 * other test file mocks out (Router.route() directly, HttpRequest.parse()
 * against an in-memory stream) here actually goes through the accept loop,
 * the worker thread, and the wire format byte-for-byte.
 */
public class EndToEndTests {

    static void run() throws Exception {
        Path webRoot = Files.createTempDirectory("http-server-e2e");
        Files.writeString(webRoot.resolve("index.html"), "hello from disk");

        Router router = new Router()
                .get("/ping", req -> HttpResponse.text(200, "OK", "pong"))
                .fallback(new StaticFileHandler(webRoot));
        HttpServer server = new HttpServer(0, router, 8);
        int port = server.start();
        try {
            testSingleRequestOverRealSocket(port);
            testTwoRequestsReuseOneKeepAliveConnection(port);
            testConnectionCloseHeaderClosesAfterOneResponse(port);
            testStaticFileServedThroughFullStack(port);
        } finally {
            server.stop();
        }
    }

    private static Socket connect(int port) throws IOException {
        return new Socket("localhost", port);
    }

    private static void send(Socket socket, String raw) throws IOException {
        socket.getOutputStream().write(raw.getBytes(StandardCharsets.UTF_8));
        socket.getOutputStream().flush();
    }

    /** Reads exactly one HTTP response (headers + Content-Length body) off the socket. */
    private static String[] readOneResponse(InputStream in) throws IOException {
        StringBuilder headLines = new StringBuilder();
        String line;
        int contentLength = 0;
        // status line
        String statusLine = readLine(in);
        headLines.append(statusLine).append('\n');
        while (!(line = readLine(in)).isEmpty()) {
            headLines.append(line).append('\n');
            if (line.toLowerCase().startsWith("content-length:")) {
                contentLength = Integer.parseInt(line.substring(line.indexOf(':') + 1).trim());
            }
        }
        byte[] body = new byte[contentLength];
        int off = 0;
        while (off < contentLength) {
            int n = in.read(body, off, contentLength - off);
            if (n == -1) break;
            off += n;
        }
        return new String[]{statusLine, headLines.toString(), new String(body, StandardCharsets.UTF_8)};
    }

    private static String readLine(InputStream in) throws IOException {
        StringBuilder sb = new StringBuilder();
        int b;
        while ((b = in.read()) != -1) {
            if (b == '\n') {
                if (sb.length() > 0 && sb.charAt(sb.length() - 1) == '\r') sb.setLength(sb.length() - 1);
                return sb.toString();
            }
            sb.append((char) b);
        }
        return sb.toString();
    }

    static void testSingleRequestOverRealSocket(int port) throws IOException {
        try (Socket socket = connect(port)) {
            send(socket, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            String[] response = readOneResponse(socket.getInputStream());
            TestHelper.assertEquals("HTTP/1.1 200 OK", response[0], "status line over the real socket");
            TestHelper.assertEquals("pong", response[2], "body over the real socket");
        }
    }

    static void testTwoRequestsReuseOneKeepAliveConnection(int port) throws IOException {
        try (Socket socket = connect(port)) {
            send(socket, "GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n");
            String[] first = readOneResponse(socket.getInputStream());
            TestHelper.assertEquals("HTTP/1.1 200 OK", first[0], "first request on a fresh keep-alive connection");

            // If the server had closed the socket, this second write/read would fail or hang;
            // it succeeding on the SAME Socket object proves the connection was kept open.
            send(socket, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            String[] second = readOneResponse(socket.getInputStream());
            TestHelper.assertEquals("pong", second[2], "second request reuses the same TCP connection as the first");
        }
    }

    static void testConnectionCloseHeaderClosesAfterOneResponse(int port) throws IOException {
        try (Socket socket = connect(port)) {
            send(socket, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            readOneResponse(socket.getInputStream());
            // Server should now close its end; a further read reaches EOF (-1) rather than blocking forever.
            int next = socket.getInputStream().read();
            TestHelper.assertEquals(-1, next, "after 'Connection: close', server closes the socket instead of waiting for another request");
        }
    }

    static void testStaticFileServedThroughFullStack(int port) throws IOException {
        try (Socket socket = connect(port)) {
            send(socket, "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            String[] response = readOneResponse(socket.getInputStream());
            TestHelper.assertEquals("HTTP/1.1 200 OK", response[0], "static file fallback reachable through the real router+socket path");
            TestHelper.assertEquals("hello from disk", response[2], "file contents match what was written to webRoot");
        }
    }
}
