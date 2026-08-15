defmodule UrlShortener.Http do
  @moduledoc """
  A tiny HTTP/1.1 layer: request parsing, response building, and routing.

  Deliberately no Phoenix/Cowboy/Plug -- the tutorial this project is
  based on (`Building a Simple Chat App With Elixir and Phoenix`) wires up
  a full Phoenix app, but the interesting part for this repo is the same
  thing every prior "build it yourself" project here has cared about: the
  bytes-on-a-socket layer, not the framework sitting in front of it.
  """

  defmodule Request do
    defstruct [:method, :path, :headers, :body]
  end

  @doc """
  Tries to parse a full HTTP request out of `buffer`.

  Returns `{:ok, request, rest}` once a full request (headers *and* body,
  per `Content-Length`) is present, or `:incomplete` if the caller needs
  to read more bytes off the socket and try again.

  `:gen_tcp.recv/2` hands back whatever bytes the kernel currently has
  buffered, not "one request" -- a request can arrive as one `recv` or as
  several, split anywhere, including mid-header-line or mid-body. This
  function does not assume otherwise: it looks for the blank-line header
  terminator before parsing anything, and re-checks the byte count against
  `Content-Length` before declaring the body complete, so it's safe to
  call repeatedly on a growing buffer.
  """
  def parse_request(buffer) when is_binary(buffer) do
    case :binary.match(buffer, "\r\n\r\n") do
      :nomatch ->
        :incomplete

      {header_end, _len} ->
        <<head::binary-size(header_end), "\r\n\r\n", rest::binary>> = buffer
        [request_line | header_lines] = String.split(head, "\r\n")

        with {:ok, method, path} <- parse_request_line(request_line) do
          headers = parse_headers(header_lines)
          content_length = header_content_length(headers)

          if byte_size(rest) >= content_length do
            <<body::binary-size(content_length), remainder::binary>> = rest
            {:ok, %Request{method: method, path: path, headers: headers, body: body}, remainder}
          else
            :incomplete
          end
        end
    end
  end

  defp parse_request_line(line) do
    case String.split(line, " ") do
      [method, path, _version] -> {:ok, method, path}
      _other -> {:error, :bad_request_line}
    end
  end

  defp parse_headers(lines) do
    Map.new(lines, fn line ->
      [name, value] = String.split(line, ":", parts: 2)
      {String.downcase(name) |> String.trim(), String.trim(value)}
    end)
  end

  defp header_content_length(headers) do
    case Map.fetch(headers, "content-length") do
      {:ok, value} -> String.to_integer(value)
      :error -> 0
    end
  end

  @doc """
  Builds the raw response bytes for `status`/`headers`/`body`.

  `Content-Length` is always recomputed from `byte_size(body)` here and
  any caller-supplied one is dropped -- the same rule
  `2026-08-13-rust-microservice/src/http.rs` and
  `2026-08-09-java-http-server` both enforce, for the same reason: a wrong
  length makes a client hang waiting for bytes that were never coming, and
  the only way to guarantee it's right is to never let a caller assert it
  independently of the body they actually handed you.
  """
  def build_response(status, headers, body) do
    reason = reason_phrase(status)

    headers =
      headers
      |> Map.delete("content-length")
      |> Map.put("Content-Length", Integer.to_string(byte_size(body)))

    header_lines =
      Enum.map(headers, fn {k, v} -> "#{k}: #{v}\r\n" end)
      |> Enum.join()

    "HTTP/1.1 #{status} #{reason}\r\n" <> header_lines <> "\r\n" <> body
  end

  defp reason_phrase(200), do: "OK"
  defp reason_phrase(201), do: "Created"
  defp reason_phrase(302), do: "Found"
  defp reason_phrase(400), do: "Bad Request"
  defp reason_phrase(404), do: "Not Found"
  defp reason_phrase(_), do: "Error"

  @doc """
  Routes one parsed `Request` to a `{status, headers, body}` reply.

  Takes no socket, so it's exercised directly in
  `test/url_shortener/http_test.exs` with hand-built `%Request{}` values --
  the same split the Rust and Java projects used their `route()` for.
  """
  def route(%Request{method: "POST", path: "/shorten", body: body}) do
    target = String.trim(body)

    cond do
      target == "" ->
        {400, %{"Content-Type" => "text/plain"}, "missing target URL in request body"}

      not valid_target?(target) ->
        {400, %{"Content-Type" => "text/plain"}, "target must start with http:// or https://"}

      true ->
        case UrlShortener.Store.create_short(target) do
          {:ok, code} ->
            {201, %{"Content-Type" => "text/plain", "Location" => "/" <> code}, code}

          {:error, :exhausted} ->
            {400, %{"Content-Type" => "text/plain"}, "could not allocate a free code, try again"}
        end
    end
  end

  def route(%Request{method: "GET", path: "/" <> rest}) do
    case String.split(rest, "/") do
      [code, "stats"] when code != "" ->
        stats_response(code)

      [code] when code != "" ->
        redirect_response(code)

      [""] ->
        {200, %{"Content-Type" => "text/plain"}, "POST a URL to /shorten to get a short code\n"}

      _other ->
        {404, %{"Content-Type" => "text/plain"}, "not found"}
    end
  end

  def route(%Request{}) do
    {404, %{"Content-Type" => "text/plain"}, "not found"}
  end

  defp redirect_response(code) do
    case UrlShortener.Store.resolve(code) do
      {:ok, target} -> {302, %{"Location" => target}, ""}
      :not_found -> {404, %{"Content-Type" => "text/plain"}, "no such code: #{code}"}
    end
  end

  defp stats_response(code) do
    case UrlShortener.Store.hits(code) do
      {:ok, hits} -> {200, %{"Content-Type" => "text/plain"}, Integer.to_string(hits)}
      :not_found -> {404, %{"Content-Type" => "text/plain"}, "no such code: #{code}"}
    end
  end

  defp valid_target?(target) do
    String.starts_with?(target, "http://") or String.starts_with?(target, "https://")
  end
end
