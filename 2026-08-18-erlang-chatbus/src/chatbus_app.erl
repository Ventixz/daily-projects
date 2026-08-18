%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_app).
-behaviour(application).

-export([start/2, stop/1]).

start(_StartType, _StartArgs) ->
    chatbus_sup:start_link().

stop(_State) ->
    ok.
