defmodule UrlShortener.Server do
  @moduledoc """
  The socket-touching half: accepts connections and drives the read/parse
  /route/write cycle for each one. `UrlShortener.Http` never sees a
  socket; this module is the only place that does.
  """

  require Logger

  def child_spec(opts) do
    port = Keyword.fetch!(opts, :port)

    %{
      id: __MODULE__,
      start: {__MODULE__, :start_link, [port]},
      restart: :permanent
    }
  end

  def start_link(port) do
    Task.start_link(__MODULE__, :listen, [port])
  end

  def listen(port) do
    {:ok, listen_socket} =
      :gen_tcp.listen(port, [:binary, packet: :raw, active: false, reuseaddr: true])

    Logger.info("url_shortener listening on port #{port}")
    accept_loop(listen_socket)
  end

  defp accept_loop(listen_socket) do
    {:ok, client} = :gen_tcp.accept(listen_socket)

    {:ok, _pid} =
      Task.Supervisor.start_child(UrlShortener.TaskSupervisor, fn -> handle_connection(client) end)

    accept_loop(listen_socket)
  end

  @doc """
  One connection, one request, then close.

  No keep-alive here, same deliberate cut `2026-08-13-rust-microservice`
  made: this project's lesson is Mnesia and the read-buffering below, not
  the connection-lifecycle state machine a keep-alive loop needs.
  Every accepted connection runs under `UrlShortener.TaskSupervisor`
  (started as a plain, unlinked `Task.Supervisor` in the application
  tree), so a crash while handling one request -- a malformed request
  line, a body larger than announced, anything `handle_connection/1`
  doesn't catch -- takes down only that one connection's process. The
  acceptor loop above never sees it and keeps accepting.
  """
  def handle_connection(socket) do
    case read_request(socket, "") do
      {:ok, request} ->
        {status, headers, body} = UrlShortener.Http.route(request)
        :gen_tcp.send(socket, UrlShortener.Http.build_response(status, headers, body))

      {:error, _reason} ->
        :ok
    end

    :gen_tcp.close(socket)
  end

  @doc """
  Reads off `socket` and hands each chunk to `Http.parse_request/1`,
  looping until it reports a complete request rather than assuming one
  `recv` is one request.
  """
  def read_request(socket, buffer) do
    case UrlShortener.Http.parse_request(buffer) do
      {:ok, request, _rest} ->
        {:ok, request}

      :incomplete ->
        case :gen_tcp.recv(socket, 0, 5_000) do
          {:ok, chunk} -> read_request(socket, buffer <> chunk)
          {:error, reason} -> {:error, reason}
        end

      {:error, reason} ->
        {:error, reason}
    end
  end
end
