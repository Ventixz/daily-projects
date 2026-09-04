# Learning: Roll Your Own IRC Bot (Haskell)

**Source:** ["Roll Your Own IRC Bot"](https://wiki.haskell.org/Roll_your_own_IRC_bot), from the
Haskell section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

An IRC bot with a hand-written protocol parser, a real TCP client, and a small set of chat
commands — split hard along a functional-core / imperative-shell line:

- `src/Irc/Message.hs` — parses and renders the IRC wire format by walking the `Text` by hand
  (prefix, command, middle params, trailing param) instead of splitting on `words`, because the
  trailing parameter is allowed to contain spaces and a naive split mangles it.
- `src/Irc/Bot.hs` — the entire protocol state machine as one pure function,
  `step :: UTCTime -> BotState -> Message -> (BotState, [Action])`. No `IO` anywhere in this
  module. It answers `PING`, joins channels on `001` (`RPL_WELCOME`), tracks which channels we're
  actually in by watching our own `JOIN` echo, and turns `PRIVMSG` lines into command dispatch.
  Also home to `reconnectDelay`, the pure backoff-schedule function.
- `src/Irc/Commands.hs` — the stateless command table (`!ping`, `!echo`, `!help`).
- `app/Main.hs` — the only module that touches a socket: opens the connection, feeds each line it
  reads into `step`, writes back whatever `Action`s come out, and wraps the whole session in a
  reconnect loop that consults `reconnectDelay`.
- `test/Spec.hs` — 34 hand-rolled assertions against the parser, the renderer, and `step` — all of
  them calling pure functions directly, none of them touching a socket.
- `test/mock/Server.hs` + `test/run_integration.sh` — a scripted fake IRC server that the real
  `bin/ircbot` binary connects to over an actual `127.0.0.1` TCP socket, so the socket-handling
  code in `app/Main.hs` gets exercised by something that isn't a unit test.

## Run it

```bash
cd 2026-09-04-haskell-irc-bot
make test    # 34 unit assertions (pure) + one real-socket integration run
make run     # same integration scenario, with the bot's full wire trace printed
```

Actual session (`make run`, trimmed to the wire trace `app/Main.hs` prints to stderr — `>>` is the
bot sending, `<<` is the bot receiving):

```
>> NICK hs-daily-bot
>> USER hs-daily-bot 0 * :daily-projects irc bot
<< :mock.server 001 hs-daily-bot :Welcome to the mock network
>> JOIN #test
<< :alice!a@host PRIVMSG #test :!ping
>> PRIVMSG #test pong!
<< :alice!a@host PRIVMSG #test :!echo hello there
>> PRIVMSG #test :hello there
<< :mock.server PING :sometoken
>> PONG sometoken
<< :alice!a@host PRIVMSG #test :!seen bob
>> PRIVMSG #test :bob hasn't been seen
<< :bob!b@host PRIVMSG #test :good morning
<< :alice!a@host PRIVMSG #test :!seen bob
>> PRIVMSG #test :bob was last seen in #test at 2026-09-04 05:21:26.647800695 UTC
<< :alice!a@host PRIVMSG hs-daily-bot :!ping
>> PRIVMSG alice pong!
```

That's a real client and a real (if scripted) server, talking actual IRC over an actual socket —
not two in-process function calls pretending to be a network.

## What it actually teaches

- **The wire protocol has one rule that breaks every naive parser: the trailing parameter is
  whatever's left after the first standalone `:`, spaces included.** `words`-splitting
  `":Angel!wings@irc.org PRIVMSG Wiz :Are you receiving this message ?"` throws away the fact that
  `"Are you receiving this message ?"` is *one* parameter, not six. `Message.hs`'s `parseParams`
  handles this with a two-line special case (`Just (':', trailing) -> Right [trailing]`) instead of
  trying to make a general tokenizer aware of it — the round-trip test
  (`"render then reparse round-trips a multi-word trailing message"`) is what would have caught it
  if that case were missing, since a `words`-based parser passes every *other* test easily.
- **A rendering decision needs a rule, not a preference, or the wire format gets ambiguous.**
  `renderMessage` adds the trailing colon exactly when a parameter is empty, contains a space, or
  itself starts with `:` — never "when I feel like it." Skipping that rule for a single-word
  reply like `pong!` isn't a shortcut, it's required: a colon on a token with no space is legal but
  pointless, so the two most natural implementations ("always colon the last param" vs. "colon only
  when necessary") produce wire-compatible but textually different output — which is exactly the
  bug the integration test's mock server caught during development, when its hardcoded expectation
  (`"PRIVMSG #test :pong!"`) didn't match what the bot actually sent (`"PRIVMSG #test pong!"`). The
  fix was the *test's* expectation, not the renderer; the renderer was already following its own
  rule correctly.
- **Keeping `IO` out of the protocol logic is what makes 34 assertions run in milliseconds with no
  server anywhere.** `step` takes the current time as a plain argument instead of calling
  `getCurrentTime` itself, so `"any message from a nick updates !seen for that nick"` can assert an
  exact, deterministic reply string instead of a fuzzy "contains a timestamp" check. `app/Main.hs`
  is the only place that ever calls `getCurrentTime`, right before handing the result to `step`.
- **A command reply has to target where the conversation actually is, and IRC doesn't tell you that
  directly.** `PRIVMSG`'s first parameter is the *destination the message was sent to* — for a
  channel message that's the channel name (`#test`), but for a direct message it's our own nick,
  which is useless as a reply target. `handlePrivmsg` has to notice that and reply to the sender
  (`prefixNick pfx`) instead, which is exactly what the "private `!ping` replies to the sender, not
  a channel" test (and the mock server's 6th scripted exchange) exists to pin down — get the
  channel/DM branch backwards and every private command reply silently vanishes into the ether
  (sent back to your own nick, which nobody is listening on) instead of erroring.
- **A capped exponential backoff has two things worth getting wrong independently: the growth and
  the cap.** `reconnectDelay attempt = min 60 (2 ^ max 0 (min attempt 6))` clamps the *exponent*
  before computing `2^attempt`, not just the final result — cheap insurance against a huge attempt
  count blowing up before `min` ever gets a chance to act, and it's also what makes
  `reconnectDelay (-5)` and `reconnectDelay 1000` both well-defined instead of one of them being an
  accident of `Int` wraparound.
- **Differential testing against a mock still needs the two ends of the wire to agree on the exact
  bytes, not just the same intent.** The `test/mock/Server.hs` process talks to `bin/ircbot`
  over a real `127.0.0.1` socket rather than calling `step` directly — that's what turned a rendering
  quirk (see the colon point above) into a caught bug instead of a passing-but-wrong test, and it's
  also what exercises `app/Main.hs`'s buffering, `noNewlineTranslation`, and EOF-triggers-clean-exit
  behavior that no pure unit test can reach.

## Deliberate scope cuts

- **No TLS.** Plain-text sockets only; a real deployment against a public network almost always
  needs `+6697`/TLS, which is a different (if not fundamentally harder) problem this project
  doesn't attempt.
- **No SASL / server password / NickServ handshake.** Registration is the bare `NICK`+`USER`
  minimum; anything requiring authentication before joining is out of scope.
- **Channel sigils limited to `#` and `&`.** RFC 2812 also allows `+` and `!`-prefixed channels;
  `isChannel` only recognizes the two that matter on every network anyone actually runs today.
- **No flood/rate limiting on outgoing messages.** A command handler that somehow produced many
  replies per incoming line would hammer the socket with no backpressure — fine for a demo bot
  answering one line at a time, not fine unattended on a real network.
- **`!seen` tracks the whole process lifetime in memory only.** No persistence across restarts;
  losing the process loses the seen-table.
- **The reconnect loop is bounded by `--max-retries` (default 5), not infinite.** A real long-running
  bot probably wants infinite retries with a capped delay (which `reconnectDelay` already
  supports) — the finite default here is mostly so the integration test doesn't hang if something
  regresses in the connection path.

## What I'd add next

- **TLS support** via the `tls`/`connection` packages, since that's the realistic next step before
  this could talk to an actual public IRC network.
- **A pluggable command interface** that lets a stateful command (like `!seen`) register itself
  without `Bot.hs`'s `dispatch` needing a hardcoded special case — right now there's exactly one
  stateful command, so the special case is the simpler design, but a second one would make the
  case for a real `Command = BotState -> [Text] -> (Maybe Text, BotState)` abstraction.
- **Persisting `stLastSeen` to disk** (even a flat file) so `!seen` survives a reconnect, which
  happens far more often in practice than a fresh process start.
- **Multi-server support** — right now `BotState` and the connection are both singular; running the
  same bot identity across several networks would need the state machine parameterized over a
  server identifier.
