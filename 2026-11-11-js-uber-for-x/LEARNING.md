# Learning: Real-time Ride Dispatch ("Uber-for-X") in Node.js

**Source:** "How to build your own Uber-for-X App using JavaScript, Node.JS, MongoDB and Web Sockets"
(Parts 1 and 2, ashwinhariharan.tech), in the Node section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Scope: my own **dependency-free** version. The tutorial uses Express, MongoDB's geospatial
queries and Socket.IO. Here those three are replaced with Node's standard library, so the parts
that are usually hidden have to be written by hand:

| Tutorial uses | Here |
|---|---|
| MongoDB `2dsphere` index + `$near` | `geo.js`: haversine distance + radius/limit filter |
| Socket.IO | `ws.js`: an RFC 6455 WebSocket server (handshake, framing, masking, ping/pong, close) |
| ad-hoc request/accept handlers | `dispatch.js`: an explicit ride state machine with offer timeouts |

Run: `npm start`, then open <http://localhost:3000> in two tabs (one driver, one rider).
Test: `npm test` (16 tests with `node:test`, Node 18+, nothing to install).

## Layout

- `geo.js`: `haversineKm`, `nearest(candidates, origin, {maxKm, limit})`, `isValidPoint`.
- `dispatch.js`: `Dispatcher` (an `EventEmitter`) with drivers, rides and the lifecycle below.
- `ws.js`: `acceptKey`, `encodeFrame`, `decodeFrame`, `upgrade()`, `WebSocketConnection`.
- `server.js`: HTTP server (`/`, `/health`) plus the `/ws` upgrade. It routes JSON messages to the dispatcher and sends its events back to the right sockets.
- `public/index.html`: a bare demo client.
- `test/`: unit tests for geo, dispatch and frame encoding, plus two end-to-end tests that drive the real server over raw TCP sockets.

## Ride lifecycle

```
request ──► searching ──► offered ──accept──► accepted ──complete──► completed
                ▲            │
                └─ decline / timeout / driver disconnects (try next-nearest driver)
searching/offered ──► no_drivers         searching/offered/accepted ──cancel──► cancelled
```

## Message protocol (JSON over WebSocket)

| From | `type` | Fields |
|---|---|---|
| any | `hello` | `role` (`driver` or `rider`), `id` |
| driver | `location` | `lat`, `lng` |
| driver | `accept` / `decline` / `complete` | `rideId` |
| rider | `request` | `lat`, `lng` |
| rider | `cancel` | `rideId` |
| server | `welcome`, `offer`, `ride`, `driver_location`, `error` | |

## What I learned

- **The WebSocket handshake is just HTTP plus one hash.** The server answers `101 Switching Protocols` with `Sec-WebSocket-Accept = base64(sha1(key + magic GUID))`. The RFC's own example key is a free test vector.
- **Framing is a tiny binary protocol.** It has a FIN bit, an opcode, a 7-bit length that escalates to 16 or 64 bits (`126`/`127`), then an optional 4-byte mask XORed over the payload. Clients *must* mask and servers *must not*. The server closes with 1002 when a client frame is unmasked.
- **TCP is a stream, not messages.** One `data` event can hold half a frame or three frames. The decoder returns `null` until a frame is complete, and the connection buffers and loops. A test feeds every prefix of a frame to prove it.
- **Control frames have their own rules.** Ping, pong and close must be ≤125 bytes and unfragmented. Pings get answered with a pong that echoes the payload.
- **Geo search is the database's job in the tutorial, but the math is short.** Haversine gives great-circle distance. A linear scan sorted by distance is fine for thousands of drivers. Beyond that you'd want a geohash, an R-tree or the database index.
- **Model dispatch as a state machine, not as callbacks.** Every transition checks the current state *and* who is asking (`only the offered driver can accept, and only once`). That rules out double-accepts and stale clicks.
- **Reserve resources while an offer is open.** A driver is marked unavailable as soon as they're offered a ride, so two concurrent requests can't both go to them. Decline, timeout, cancel and disconnect all release the driver.
- **Inject timers.** Passing `setTimer`/`clearTimer` into the dispatcher made offer timeouts testable without sleeps. One test also checks that accepting clears the pending timer.
- **Keep the core free of I/O.** `Dispatcher` knows nothing about sockets and only emits events. `server.js` is a thin adapter, so most of the logic is tested synchronously.

## Known limits

- All state is in memory. A restart loses drivers and rides, and it can't scale to several processes without shared state (e.g. Redis pub/sub).
- There is no authentication: anyone can `hello` as any driver or rider id that isn't already connected.
- WebSocket support covers only what this app needs: no binary messages, extensions (compression), subprotocols, UTF-8 validation or server-initiated keep-alive pings.
- Nearest-driver search is a linear scan by straight-line distance, not road distance or ETA.
