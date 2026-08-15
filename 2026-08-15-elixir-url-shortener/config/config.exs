import Config

config :mnesia, dir: ~c"Mnesia.#{Mix.env()}.#{node()}"

config :url_shortener, port: 4000

import_config "#{config_env()}.exs"
