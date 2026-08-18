# ChatBus: A Multi-User Chat Room with Erlang/OTP (Erlang)

**Source:** ["ChatBus: build your first multi-user chat room app with
Erlang/OTP"](https://medium.com/@kansi/chatbus-build-your-first-multi-user-chat-room-app-with-erlang-otp-b55f72064901)
by Kansi, from the Erlang section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries -- `medium.com` gets a flat `EGRESS_BLOCKED` -- so
what's here isn't a port of that specific post's code. It's the project
the title actually describes: a TCP chat server built the OTP way, with
a supervision tree instead of a pile of loosely-coordinated processes,
built from how `gen_server`/`supervisor`/`gen_tcp` are meant to fit
together, the way the tutorial's own title promises.

## What it is

A multi-room TCP chat server, structured as a proper OTP application
with a four-level supervision tree instead of one big process doing
everything:

- `chatbus_sup` -- top-level `one_for_one` supervisor, starts the other
  three in dependency order.
- `chatbus_room_sup` -- a `simple_one_for_one` supervisor that spawns
  `chatbus_room` workers on demand, one per room name, with an `ets`
  table doing name-to-`Pid` lookup and dedup.
- `chatbus_client_sup` -- a `simple_one_for_one` supervisor that spawns
  one `chatbus_client` `gen_server` per accepted TCP connection.
- `chatbus_listener` -- not a `gen_server` at all; a `proc_lib` process
  that blocks in `gen_tcp:accept/1` forever and hands each new socket to
  `chatbus_client_sup`.
- `chatbus_room` -- the actual chat logic: membership, join/leave
  notifications, and broadcast, entirely in terms of Erlang messages
  sent to member `Pid`s. It has never heard of TCP.
- `test/chatbus_room_tests.erl` -- 6 EUnit tests against `chatbus_room`
  and `chatbus_room_sup` directly, no sockets involved.
- `test/chatbus_integration_tests.erl` -- 2 EUnit tests that start the
  whole `chatbus` application and drive it over real `gen_tcp` sockets.

## Run it

```bash
cd 2026-08-18-erlang-chatbus
make test                 # 8 EUnit tests: 6 unit + 2 full-stack integration
make run                  # starts the OTP app on port 5555, drops into an erl shell
```

In another terminal:

```bash
nc localhost 5555
NICK alice
JOIN lobby
MSG hello room
QUIT
```

Open a second `nc` session and `JOIN lobby` from it too, to see the
broadcast and the join/leave notifications arrive live.

## What it actually teaches

- **A supervision tree is a dependency graph, and the child list order
  is the whole guarantee.** `chatbus_sup:init/1` starts
  `chatbus_room_sup`, then `chatbus_client_sup`, then
  `chatbus_listener`, in that order, because the listener hands every
  accepted socket to `chatbus_client_sup:start_client/1` and that
  registered name has to already exist. `supervisor:init/1` starts
  children sequentially, blocking on each child's own `init` returning
  before starting the next -- so putting them in dependency order *is*
  the synchronization, not a convenience.
- **`simple_one_for_one` is what "spawn one of these per X, on demand"
  actually looks like in OTP**, as opposed to statically listing every
  child in `chatbus_sup`'s child spec (which would mean knowing every
  room and every connected client in advance, which is nonsense for
  either one). `chatbus_room_sup` and `chatbus_client_sup` both use it:
  the spec lists one child *template*, and `start_child(Sup, [Arg])`
  appends `Arg` to that template's start args at spawn time.
- **A supervisor doesn't deduplicate by argument, so "only one room per
  name" has to be built on top of it, and `ets:insert_new/2` is the
  actual atomicity primitive, not a coordinating process.**
  `chatbus_room_sup:get_or_start_room/1` can have two clients both call
  it for the same brand-new room name in the same few microseconds;
  both `supervisor:start_child/2` calls succeed and both rooms actually
  start, because the supervisor has no idea "same name" should mean
  "same room". The dedup happens after the fact: whichever call's
  `ets:insert_new({Name, Pid})` lands first wins the name, and the loser
  tears its own (already-running) room back down and adopts the
  winner's `Pid` instead. Getting this right without a lock required
  understanding that `ets:insert_new/2` is atomic per key across
  concurrent callers and `ets:insert/2` is not.
- **The chat logic (`chatbus_room`) never imports `gen_tcp`, and that
  split is what makes it unit-testable without a socket.**
  `chatbus_room_tests.erl` joins `self()` into a room and asserts on
  `chat_msg`/`room_event` tuples arriving in its own mailbox directly --
  no listening socket, no client process, no network stack. The
  translation from "Erlang message in a mailbox" to "line of text on a
  socket" is `chatbus_client`'s entire job and nobody else's.
- **`erlang:monitor/2` is what makes "the client crashed" and "the
  client said LEAVE" the same code path, and skipping it silently
  breaks cleanup.** `chatbus_room:join/3` monitors the joining `Pid`;
  `crashed_member_is_reaped_via_monitor_test` kills a member with
  `exit(Victim, kill)` -- no `LEAVE` message, no graceful shutdown -- and
  the room still emits a `left` notification and drops it from
  `members/1`, because the `'DOWN'` message and an explicit `leave`
  call both funnel into removing the same map entry.
- **A socket handoff has an ownership race, and getting the order wrong
  silently drops the first messages.** `chatbus_listener` calls
  `gen_tcp:controlling_process(Socket, ClientPid)` *before* casting
  `activate` to the new client. The first version of this code had
  `chatbus_client:init/1` cast `activate` to itself, which raced against
  the listener's `controlling_process` call and would occasionally set
  `{active, once}` on a socket the client process didn't own yet.
  Sequencing "become the owner" strictly before "start receiving" is
  what closes that window, not a retry or a timeout.
- **Broadcast reaching the sender too is a real design choice, not an
  edge case to special-case away.** `chatbus_room:broadcast/3` messages
  every member's `Pid`, including whoever's `MSG` triggered it, so a
  client sees its own line echoed back exactly like everyone else's.
  `chatbus_integration_tests.erl` originally didn't drain that echo
  before asserting on the *next* line, and got the previous message
  back instead -- a genuine bug in the test, not the server, and a
  concrete demonstration that this protocol's ordering guarantee is "one
  socket, one FIFO queue," full stop.

## Deliberate scope cuts

- **Rooms are created on first `JOIN` and never torn down.** An empty
  room's `chatbus_room` process just sits idle forever. Reaping empty
  rooms would mean the room deciding when it's "done", which is a
  genuinely different lifecycle question (idle timeout? explicit
  `/close`? last-member-leaves?) from anything this project needed to
  answer to teach the supervision-tree shape.
- **No persistence.** Room membership and history live only in each
  `chatbus_room` process's state; a server restart drops everyone.
  Adding it would mean picking a storage layer, which is orthogonal to
  the OTP process-and-supervision structure this project is about.
- **No distribution.** Everything runs in one BEAM node. Erlang's
  message-passing model is what makes multi-node practically trivial
  (the same `Pid ! Msg` works across a node boundary once nodes are
  connected), but wiring up actual clustering is a separate exercise
  from the local process tree this one builds.
- **Plain-text line protocol, no framing beyond `\n`.** No lengths, no
  binary framing, no TLS. Fine for `nc`, not for anything adversarial.

## What I'd add next

- **Idle-room reaping**, since it's the most obvious gap between "demo"
  and "server you'd actually run", and it's a natural extension of the
  `ets` registry already tracking every room's `Pid`.
- **A `LIST` command** enumerating live rooms and their member counts,
  which is mostly free given `chatbus_room_sup`'s registry already has
  every name-to-`Pid` mapping.
- **Distribution across nodes**, to make concrete the thing that's
  usually just asserted about Erlang: that `chatbus_room:broadcast/3`'s
  `Pid ! Msg` line wouldn't need to change at all if `Members` held
  `Pid`s living on a different node.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Built from ["ChatBus: build your first multi-user chat room app with
Erlang/OTP"](https://medium.com/@kansi/chatbus-build-your-first-multi-user-chat-room-app-with-erlang-otp-b55f72064901)
by Kansi.
