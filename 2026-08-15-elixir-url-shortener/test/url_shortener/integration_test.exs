defmodule UrlShortener.IntegrationTest do
  use ExUnit.Case, async: false

  # The application (started by `mix test` itself) already has
  # `UrlShortener.Server` listening on `config :url_shortener, port:`
  # (4999 in `config/test.exs`) -- these tests are real TCP clients
  # talking to that real, already-running server, not calls into
  # `Http.route/1` directly.
  @port 4999

  defp request(raw) do
    {:ok, socket} = :gen_tcp.connect(~c"localhost", @port, [:binary, active: false])
    :ok = :gen_tcp.send(socket, raw)
    response = recv_all(socket, "")
    :gen_tcp.close(socket)
    response
  end

  defp recv_all(socket, acc) do
    case :gen_tcp.recv(socket, 0, 1_000) do
      {:ok, chunk} -> recv_all(socket, acc <> chunk)
      {:error, :closed} -> acc
      {:error, :timeout} -> acc
    end
  end

  test "a full shorten -> redirect round trip over real sockets" do
    target = "https://example.com/integration"

    shorten_response =
      request(
        "POST /shorten HTTP/1.1\r\nContent-Length: #{byte_size(target)}\r\n\r\n#{target}"
      )

    assert shorten_response =~ "HTTP/1.1 201 Created"
    [_, code] = Regex.run(~r"Location: /(\w+)", shorten_response)

    redirect_response = request("GET /#{code} HTTP/1.1\r\n\r\n")
    assert redirect_response =~ "HTTP/1.1 302 Found"
    assert redirect_response =~ "Location: #{target}"
  end

  test "a request that arrives byte-by-byte still parses correctly" do
    target = "https://example.com/slow-drip"
    body = target
    raw = "POST /shorten HTTP/1.1\r\nContent-Length: #{byte_size(body)}\r\n\r\n#{body}"

    {:ok, socket} = :gen_tcp.connect(~c"localhost", @port, [:binary, active: false])

    for <<byte::binary-size(1) <- raw>> do
      :ok = :gen_tcp.send(socket, byte)
    end

    response = recv_all(socket, "")
    :gen_tcp.close(socket)

    assert response =~ "HTTP/1.1 201 Created"
  end

  test "an unknown code round trips as a real 404 over the wire" do
    response = request("GET /nosuch0 HTTP/1.1\r\n\r\n")
    assert response =~ "HTTP/1.1 404 Not Found"
  end
end
