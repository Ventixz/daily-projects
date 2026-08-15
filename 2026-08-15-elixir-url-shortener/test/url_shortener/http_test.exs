defmodule UrlShortener.HttpTest do
  use ExUnit.Case, async: false

  alias UrlShortener.Http
  alias UrlShortener.Http.Request

  describe "parse_request/1" do
    test "returns :incomplete when the header terminator hasn't arrived yet" do
      assert :incomplete = Http.parse_request("GET /abc HTTP/1.1\r\nHost: x")
    end

    test "parses a header-only GET with no body" do
      raw = "GET /abc123 HTTP/1.1\r\nHost: localhost\r\n\r\n"
      assert {:ok, %Request{method: "GET", path: "/abc123", body: ""} = req, ""} = Http.parse_request(raw)
      assert req.headers["host"] == "localhost"
    end

    test "waits for the full body when Content-Length says more is coming" do
      raw = "POST /shorten HTTP/1.1\r\nContent-Length: 20\r\n\r\nhttps://ex"
      assert :incomplete = Http.parse_request(raw)
    end

    test "parses a POST once Content-Length bytes of body are present" do
      target = "https://example.com"
      raw = "POST /shorten HTTP/1.1\r\nContent-Length: #{byte_size(target)}\r\n\r\n#{target}"
      assert {:ok, %Request{method: "POST", path: "/shorten", body: ^target}, ""} = Http.parse_request(raw)
    end

    test "leaves bytes past one complete request in the returned remainder" do
      first = "GET / HTTP/1.1\r\n\r\n"
      second = "GET /next HTTP/1.1\r\n\r\n"
      assert {:ok, %Request{path: "/"}, rest} = Http.parse_request(first <> second)
      assert rest == second
    end
  end

  describe "build_response/3" do
    test "recomputes Content-Length from the actual body regardless of what's passed in" do
      response = Http.build_response(200, %{"content-length" => "999"}, "hi")
      assert response =~ "Content-Length: 2\r\n"
      refute response =~ "999"
    end

    test "renders the status line with its reason phrase" do
      assert Http.build_response(404, %{}, "") =~ "HTTP/1.1 404 Not Found\r\n"
    end

    test "body follows the blank line separating headers from content" do
      response = Http.build_response(200, %{"x" => "y"}, "body-text")
      assert String.ends_with?(response, "\r\n\r\nbody-text")
    end
  end

  describe "route/1" do
    test "POST /shorten with an empty body is a 400" do
      req = %Request{method: "POST", path: "/shorten", headers: %{}, body: ""}
      assert {400, _headers, _body} = Http.route(req)
    end

    test "POST /shorten with a non-http target is a 400" do
      req = %Request{method: "POST", path: "/shorten", headers: %{}, body: "ftp://nope"}
      assert {400, _headers, _body} = Http.route(req)
    end

    test "POST /shorten with a valid target creates a code" do
      req = %Request{method: "POST", path: "/shorten", headers: %{}, body: "https://example.com/route-test"}
      assert {201, headers, code} = Http.route(req)
      assert headers["Location"] == "/" <> code
      assert {:ok, "https://example.com/route-test"} = UrlShortener.Store.resolve(code)
    end

    test "GET /<code> redirects to the stored target" do
      {:ok, code} = UrlShortener.Store.create_short("https://example.com/redirect-test")
      req = %Request{method: "GET", path: "/" <> code, headers: %{}, body: ""}
      assert {302, %{"Location" => "https://example.com/redirect-test"}, ""} = Http.route(req)
    end

    test "GET /<unknown> is a 404" do
      req = %Request{method: "GET", path: "/nope00", headers: %{}, body: ""}
      assert {404, _headers, _body} = Http.route(req)
    end

    test "GET /<code>/stats reports the click count" do
      {:ok, code} = UrlShortener.Store.create_short("https://example.com/stats-test")
      UrlShortener.Store.resolve(code)
      UrlShortener.Store.resolve(code)

      req = %Request{method: "GET", path: "/" <> code <> "/stats", headers: %{}, body: ""}
      assert {200, _headers, "2"} = Http.route(req)
    end

    test "GET / is a usage message, not a lookup" do
      req = %Request{method: "GET", path: "/", headers: %{}, body: ""}
      assert {200, _headers, _body} = Http.route(req)
    end
  end
end
