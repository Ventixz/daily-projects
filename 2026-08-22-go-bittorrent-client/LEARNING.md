# Building a BitTorrent Client from the Ground Up (Go)

**Source:** ["Building a BitTorrent client from the ground up in
Go"](https://blog.jse.li/posts/torrent/) by Jesse Li, from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of
package registries, so there's no real tracker to announce to and no public
swarm to pull real pieces from. What's here is the part of the tutorial that
doesn't need either: a from-scratch bencode codec, `.torrent` metainfo
parsing with a correctly-derived `info_hash`, the full peer wire protocol
(handshake, message framing, block requests), and a multi-peer piece-download
scheduler that verifies every piece's SHA-1 hash. The one piece that
genuinely needs a live network -- talking to a real HTTP tracker -- is
implemented for real (it makes an actual `net/http` request) and is tested
against a local `httptest` server standing in for the tracker, the same way
the download path is tested against a local peer standing in for the swarm.

## What it is

- `bencode/bencode.go` -- a decoder/encoder for the four bencode types
  (`i42e` integers, `4:spam` length-prefixed byte strings, `l...e` lists,
  `d...e` dictionaries with lexically sorted keys). Decodes into plain
  `int64` / `string` / `[]interface{}` / `map[string]interface{}`, no
  reflection or struct tags.
- `torrentfile/torrentfile.go` -- parses a single-file `.torrent`'s
  top-level dict, and derives `InfoHash` by re-encoding the decoded `info`
  sub-dict and taking its SHA-1 -- the same bytes a real client would hash,
  because `Encode` always emits sorted keys and a spec-conformant file was
  already sorted going in.
- `p2p/handshake.go`, `p2p/message.go`, `p2p/peer.go` -- the wire protocol:
  the 68-byte handshake, length-prefixed messages (`choke`, `unchoke`,
  `interested`, `have`, `bitfield`, `request`, `piece`, `cancel`), a
  `Bitfield` bit-per-piece accessor, and compact (6-bytes-per-peer) peer
  list parsing.
- `tracker/tracker.go` -- builds the GET URL for an HTTP tracker announce
  (BEP 3: `info_hash`, `peer_id`, `port`, `left`, `compact=1`) and parses
  the bencoded response into a peer list.
- `client/client.go` -- one handshaken connection to a peer: send/receive
  the handshake, read the opening bitfield, track choke state, apply
  incoming `have` messages to the bitfield as they arrive.
- `client/download.go` -- the scheduler: a shared work queue of
  `(pieceIndex, expectedHash, length)`, one goroutine per peer pulling from
  it, block-by-block requests capped at a backlog of 5 in-flight 16 KiB
  requests per peer, SHA-1 verification on every completed piece before
  it's accepted (a failed piece goes back on the queue for another peer).
- `cmd/gotorrent` -- the CLI: `-torrent file.torrent -out path` announces
  to the real tracker; `-peer host:port` bypasses the tracker and connects
  to one peer directly.
- `cmd/demoseeder` / `cmd/gentorrent` -- a minimal peer that serves one
  file over the real wire protocol, and a `.torrent` generator, so the
  whole pipeline can run against `resources/demo.txt` with no external
  network at all.
- `client/client_test.go`'s `TestDownloadEndToEndOverLoopback` builds a
  100,000-byte in-memory file, a fake seeder on a loopback TCP listener,
  and asserts the real `Download()` function reproduces it byte-for-byte
  across multiple pieces and multiple 16 KiB blocks per piece.

## Run it

```bash
cd 2026-08-22-go-bittorrent-client
make test   # go test ./... -- bencode, torrentfile, p2p, tracker, and the loopback download test
make demo   # generate a .torrent for resources/demo.txt, seed it, download it, diff the result
```

```
gentorrent: wrote resources/demo.torrent (232200 bytes, 8 pieces)
demoseeder: serving resources/demo.txt (232200 bytes, 8 pieces) on 127.0.0.1:6881
downloading "demo.txt" (232200 bytes, 8 pieces) from 1 peer(s)
wrote 232200 bytes to /tmp/.../demo.out.txt
OK: downloaded file is byte-identical to resources/demo.txt
```

## What it actually teaches

- **The `info_hash` is a hash of *bytes*, not of a data structure, and
  re-deriving it means re-serializing, not re-hashing whatever your parser
  happened to build.** The natural instinct is to decode the `.torrent`
  file into a Go struct and hash *that* somehow -- but a Go struct has no
  canonical byte representation. The actual protocol wants SHA-1 of the
  literal bencoded `info` dict as it appeared on the wire. My decoder
  keeps the info dict as a `map[string]interface{}` for exactly this
  reason: `Encode(reencoded) == original bytes` only holds because
  bencode's own spec forces sorted keys, so decode-then-reencode is a
  no-op on anything that was valid to begin with.
- **A dict decoded into a Go `map` has already lost the one property that
  made it hashable correctly the first time -- key order -- and you have
  to put it back deliberately.** `TestEncodeSortsDictKeys` exists because
  my first `encodeDict` iterated the map directly; Go randomizes map
  iteration order on purpose, so the same decoded dict produced a
  different byte string, and therefore a different `info_hash`, on
  successive runs of the same test. `sort.Strings` on the key slice before
  encoding fixed it, and now it's deterministic by construction rather
  than by accident of Go's runtime that particular run.
- **A block request has to be re-requested by the peer, not assumed
  delivered, and the reference algorithm's backlog counter is what makes
  "one block requested, wrong piece delivered" not silently corrupt the
  buffer.** `p2p.ParsePiece` checks the piece index *and* the begin offset
  against the destination buffer's length before copying -- I first wrote
  it trusting `begin` blindly, and `TestParsePieceRejectsOverflow` (a
  block whose `begin+len(block)` exceeds the buffer) is there because nothing
  in the wire format itself prevents a malformed or malicious peer from
  sending a block that overruns a piece boundary.
- **The download scheduler's correctness depends on piece length being
  computed identically in three unrelated places: the hash that was
  computed at `.torrent`-build time, the buffer size the client allocates
  before requesting, and the byte range a seeder slices out to answer a
  request.** My first version of the test's fake seeder recomputed piece
  length as `ceil(contentLen / numPieces)` instead of using the actual
  fixed piece length the real downloader used -- both produced a valid
  *count* of pieces, but different byte boundaries per piece, so every
  single piece failed its SHA-1 check even though every individual
  component (bencode, hashing, wire messages) was correct in isolation.
  That's a class of bug unit tests on each package miss entirely; only the
  end-to-end loopback test caught it.
- **`go test`'s default per-package pass/fail hides a deadlock as a
  timeout, not a crash, and BitTorrent's peer-choke protocol has a natural
  deadlock shape if you get the request/response order wrong.** The
  scheduler only sends block requests once `Choked` is false, which only
  flips on an incoming `unchoke` message -- get the "send interested, then
  wait for unchoke before requesting" sequencing wrong and the test just
  hangs at `go test`'s 10-minute default timeout instead of failing fast.
  `TestDownloadEndToEndOverLoopback` wraps the download in a `select` with
  its own 10-second `time.After`, specifically so a protocol-ordering bug
  reads as "did not complete within 10s" instead of a build hanging for
  10 minutes before anyone notices.

## Deliberate scope cuts

- **No real tracker, no real swarm.** `tracker.RequestPeers` makes a real
  HTTP GET and is real code, but this sandbox can't reach a real tracker
  to prove it end-to-end -- its test uses `httptest.NewServer` instead.
  The download path is proven end-to-end against `demoseeder`/a loopback
  fake peer, not the public BitTorrent network.
- **Single-file torrents only.** No `files` list, no directory layout;
  `torrentfile.Parse` rejects a multi-file `info` dict outright rather
  than mis-parsing it.
- **No DHT, no magnet links, no PEX, no UDP trackers.** Peer discovery is
  HTTP-tracker-only (or `-peer host:port` for the bypass demo).
- **No endgame mode, no choking algorithm on the serving side, no rarest
  -first piece selection.** Pieces are requested in queue order; the demo
  seeder always has everything, so there was nothing to make rarest-first
  observable without inventing a multi-peer partial-availability scenario
  that the tutorial itself doesn't cover.
- **No resume support.** A partial download isn't checkpointed; killing
  `gotorrent` mid-transfer loses the pieces it already verified.

## What I'd add next

- **A multi-peer test with partial availability** -- two fake seeders,
  each missing a different piece via their bitfields -- to actually
  exercise the "peer doesn't have it, try another" requeue path, which
  the current single-seeder test never triggers.
- **Endgame mode**, so the last few pieces get requested from every peer
  that claims to have them concurrently, instead of waiting on whichever
  single peer happened to claim the work.
- **A UDP tracker client** (BEP 15), since most trackers in the wild have
  moved off HTTP and this client currently can't talk to them at all.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Built from ["Building a BitTorrent client from the ground up in
Go"](https://blog.jse.li/posts/torrent/) by Jesse Li.
