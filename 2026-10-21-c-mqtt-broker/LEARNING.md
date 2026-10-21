# Learning: An MQTT Broker from Scratch, in C

**Source:** "MQTT Broker from scratch" (codepr) from the C/C++ → Network programming section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Written from the MQTT 3.1.1 spec using only libc and POSIX sockets.

Try it:

```
make
./broker 1883            # listens on 127.0.0.1 (port 0 picks a free one and prints it)
```

Test: `make test` runs 63 unit checks (`test_mqtt.c`) and an end-to-end script (`e2e_test.py`) that starts the
broker and speaks raw MQTT to it over sockets with several clients.

## Layout

- `mqtt.c` / `mqtt.h`: the protocol, with no I/O. Varint codec, packet framing, CONNECT / PUBLISH / SUBSCRIBE
  parsers, packet builders, topic-filter validation and matching.
- `broker.c`: a single-threaded `poll()` loop with per-client input/output buffers, subscriptions, delivery,
  session takeover and keepalive timeouts.

## What I learned

- **TCP is a stream, so framing comes first.** `mqtt_frame` returns "complete packet of N bytes", "need more"
  or "malformed" and the read loop is built on that three-way answer. Testing the "need more" case by cutting
  a packet one byte short caught more off-by-ones than any happy path did.
- **The remaining length is a 1–4 byte base-128 varint.** The spec's boundary table (127/128, 16383/16384,
  ...) makes a ready-made test; a fifth continuation byte is a protocol error, not more data.
- **Parse with bounds checks at every length prefix.** Every string is `u16 length + bytes`; a hostile length
  that runs past the packet is the classic bug, so one `read_str` helper owns that check.
- **Wildcard matching is tricky at the edges.** `a/#` also matches `a`, `+` matches an empty level
  (`/finance` matches `+/+`), and `#` or `+` at the root must not match `$SYS/...`. Writing the table of
  cases first as tests made the small iterative matcher straightforward.
- **Never write to a socket from the handler.** Replies go into a per-client output buffer that `poll()` drains
  on `POLLOUT`; otherwise one slow subscriber would stall everyone. `MSG_NOSIGNAL` plus ignoring `SIGPIPE`
  stops a dead peer from killing the broker.
- **Closing a client while iterating clients is a hazard.** A client taken over by a duplicate client id can be
  closed in the middle of handling another's packet, so the loop re-checks that the slot's fd still matches
  the one it polled.
- **My own tests had bugs too.** Three hand-assembled packets had wrong remaining-length bytes and failed
  with correct code. And an end-to-end check I had padded with `or True` was hiding a real bug: a v3.1
  client (`MQIsdp`) was dropped silently instead of getting CONNACK 0x01 "unacceptable protocol version".

## Known limits

- QoS 0 delivery only: QoS 1 publishes are acknowledged but delivered at QoS 0, QoS 2 closes the connection.
  SUBACK therefore always grants QoS 0.
- No retained messages, no persistent sessions (clean session is assumed), will messages are ignored, and
  username/password are not checked.
- Fixed limits: 128 clients, 32 subscriptions each, 64 KiB maximum packet, 16 filters per SUBSCRIBE.
- Subscription lookup scans every client's filters per publish (O(clients × filters)). A topic trie would
  scale better; the original tutorial uses one.
- Binds to loopback only, no TLS.
