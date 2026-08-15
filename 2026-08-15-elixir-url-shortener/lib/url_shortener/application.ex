defmodule UrlShortener.Application do
  @moduledoc false

  use Application

  @impl true
  def start(_type, _args) do
    UrlShortener.Store.init_schema()

    port = Application.get_env(:url_shortener, :port, 4000)

    children = [
      {Task.Supervisor, name: UrlShortener.TaskSupervisor},
      {UrlShortener.Server, port: port}
    ]

    opts = [strategy: :one_for_one, name: UrlShortener.Supervisor]
    Supervisor.start_link(children, opts)
  end
end
