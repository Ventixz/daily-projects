# Build a Network Stack with Ruby (Ruby)

**Source:** ["How to build a network stack in Ruby"](https://medium.com/geckoboard-under-the-hood/how-to-build-a-network-stack-in-ruby-f73aeb1b661b)
by the Geckoboard engineering team, from the Ruby section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
This environment's network only reaches GitHub and a short allowlist of package
registries -- `medium.com` gets a flat `EGRESS_BLOCKED` -- so what's here isn't a
port of that specific post's code. It's the project the title actually describes:
a real Ethernet/IPv4/TCP/UDP stack that parses and builds wire-format bytes and
verifies Internet checksums, the way the tutorial's own title promises. The
original post captures live traffic with a raw socket, which needs root and a
real interface; this one reads and writes standard libpcap capture files
instead, so the whole thing runs and tests deterministically without any
privileged access.

## What it is

A from-scratch decoder/encoder for four wire formats, each one only knowing
about its own header and handing the remaining bytes to whichever layer its
own header names next:

- `lib/network_stack/checksum.rb` -- the RFC 1071 Internet checksum (16-bit
  one's-complement sum, folded, complemented) plus the 12-byte TCP/UDP
  pseudo-header.
- `lib/network_stack/ethernet.rb` -- Ethernet II framing: 6-byte dst/src MAC,
  2-byte EtherType, payload.
- `lib/network_stack/ip.rb` -- IPv4 header (options-free, IHL fixed at 5
  words), including its own header checksum.
- `lib/network_stack/tcp.rb` -- TCP header (options-free, 20 bytes), flags as
  a bitmask, a tcpdump-style `flag_summary`.
- `lib/network_stack/udp.rb` -- UDP header (8 bytes) plus the RFC 768
  zero-means-no-checksum-so-remap-to-0xffff edge case.
- `lib/network_stack/pcap_file.rb` -- reader/writer for the classic
  little-endian libpcap capture format (global header + per-packet records),
  so this exercises real binary file parsing, not just byte arrays built and
  consumed in the same process.
- `lib/network_stack/decoder.rb` -- walks a raw frame down through all four
  layers and produces a tcpdump-style one-line summary.
- `bin/decode_pcap.rb` -- a minimal `tcpdump -n -r`: decode every packet in a
  `.pcap` file and print one summary line each, flagging any checksum
  mismatch.
- `resources/generate_sample_pcap.rb` -- builds `resources/sample.pcap` using
  this project's *own* builders: a TCP three-way handshake, an HTTP request
  segment, and a DNS-shaped UDP query. Round-tripping through the project's
  own writer means the test suite exercises real captured-looking bytes
  without a live capture.
- `test/` -- 30 assertions across seven files: one per layer, one for the
  pcap reader/writer, one for the decoder, and an integration test that runs
  the actual CLI against the bundled sample capture as a subprocess.

## Run it

```bash
cd 2026-08-21-ruby-network-stack
make test      # regenerates resources/sample.pcap, then 92 assertions, 0 failures
make decode    # ruby bin/decode_pcap.rb resources/sample.pcap
```

```
 1 22:13:20.000000 IP 10.0.0.1.51000 > 10.0.0.2.80: Flags [S], seq 1000, ack 0, win 64240, length 0
 2 22:13:20.000200 IP 10.0.0.2.80 > 10.0.0.1.51000: Flags [S.], seq 5000, ack 1001, win 65160, length 0
 3 22:13:20.000400 IP 10.0.0.1.51000 > 10.0.0.2.80: Flags [.], seq 1001, ack 5001, win 64240, length 0
 4 22:13:20.000600 IP 10.0.0.1.51000 > 10.0.0.2.80: Flags [P.], seq 1001, ack 5001, win 64240, length 38
 5 22:13:21.000000 IP 10.0.0.1.53211 > 10.0.0.2.53: UDP, length 30
```

## What it actually teaches

- **The Internet checksum's self-verification trick only works one way, and
  I had it backwards on the first pass.** The algorithm is: sum the header as
  16-bit words with the checksum field zeroed, one's-complement the sum, and
  write that into the field. Verifying by re-running the same sum over the
  header *as received* -- checksum field included -- does **not** reproduce
  0xffff; it reproduces **0**, because a value and its one's-complement
  always sum to 0xffff, and `~0xffff & 0xffff == 0`. My first `Checksum.valid?`
  checked for `== 0xffff` and every single packet I generated failed
  validation on its own round trip -- a very fast, very concrete lesson that
  "recompute and compare" needs the right target, not just the right
  algorithm.
- **A `Struct.new(...) do ... end` block does not give constants defined
  inside it the struct's own namespace -- Ruby constant assignment is
  lexically scoped to where the code is *written*, not to `self` at
  runtime.** `IPv4::HEADER_BYTES`, `TCP::HEADER_BYTES`, and `UDP::HEADER_BYTES`
  were all originally defined as `HEADER_BYTES = 20` inside a `Struct.new do
  ... end` block, and Ruby silently collapsed all three into one
  `NetworkStack::HEADER_BYTES`, with a "already initialized constant" warning
  as the only clue. Switching to `class IPv4 < Struct.new(:field, ...)` opens
  a real lexical scope, so `HEADER_BYTES` inside it correctly becomes
  `IPv4::HEADER_BYTES`. Same fix applied to `Ethernet`, `TCP`, and `UDP`.
- **UDP's length field is header-plus-payload, and forgetting that breaks
  checksum math in a way that "looks read" but is wrong.** I hand-crafted a
  test around the RFC 768 zero-checksum-means-no-checksum edge case (a
  computed checksum of exactly 0 has to be transmitted as 0xffff instead, so
  it's never confused with "no checksum present") and got the wrong number
  on the first attempt, because my by-hand checksum arithmetic used
  `length = 8` (the header alone) while the real code correctly used
  `length = header + payload`. The bug was in my test's expected value, not
  the implementation -- but it only surfaced because the test computed an
  independent expected checksum by hand instead of asserting against
  whatever the code happened to produce.
- **A TCP/UDP checksum can't be verified by the transport layer alone --
  it's computed over a pseudo-header (source IP, destination IP, protocol
  number, segment length) that neither header actually contains on the
  wire.** `test/tcp_test.rb`'s "checksum covers the pseudo-header" test makes
  this concrete: the exact same `TCP` segment produces two different wire
  checksums depending only on which source IP is fed into
  `Checksum.pseudo_header` before hashing. That's deliberate: it's what
  stops a segment from validating successfully after being misdelivered to
  the wrong host, since the addressing that matters for delivery isn't
  inside the segment that's actually being checksummed.
- **Parsing a real binary file format (libpcap) surfaces framing bugs that
  in-memory byte arrays never do.** `PcapFile.each_packet` reads a 24-byte
  global header, then loops reading fixed 16-byte record headers followed by
  a variable `incl_len` of packet bytes -- and the boundary case that matters
  is `f.read(RECORD_HEADER_BYTES)` returning `nil` cleanly at true EOF versus
  returning fewer bytes than expected mid-record (a truncated/corrupt
  capture), which have to be handled differently: the first is just "no more
  packets," the second is a real `ArgumentError`.

## Deliberate scope cuts

- **No live packet capture.** The original tutorial opens a raw socket and
  reads real traffic off an interface, which needs root/`CAP_NET_RAW` and
  isn't available (or appropriate) in this environment. Everything here
  works from libpcap files instead -- the same wire bytes a raw socket would
  hand you, just sourced from a `.pcap` file rather than a live NIC. Nothing
  in the parsing/building code would change to point it at a real socket;
  only `bin/decode_pcap.rb`'s input source would.
- **No IP options, no TCP options.** Both parsers assert IHL == 5 / data
  offset == 20 and raise on anything else, rather than silently
  mis-parsing a header whose real length they didn't account for.
- **No fragmentation reassembly, no TCP retransmission/reordering.** This
  decodes one frame at a time: no state is kept across packets, so a
  fragmented IP datagram or a TCP stream split across segments is shown as
  independent packets, not reassembled into the original message.
- **IPv4 only.** No IPv6; EtherType 0x86DD is unrecognized by `Decoder`
  the same as any other non-IPv4 EtherType.

## What I'd add next

- **TCP option parsing** (MSS, window scale, SACK-permitted), since real
  captures almost never have a bare 20-byte TCP header and the current
  `data_offset == HEADER_BYTES` check would reject nearly every packet
  a live capture would actually contain.
- **IP fragment reassembly**, which would turn `Decoder` from "one frame at
  a time" into something that can recover the original datagram a sender
  split across several IP packets.
- **A pcap*ng* reader**, since that's what current Wireshark writes by
  default and the classic format used here is the older of the two.

## License

Licensed under the MIT License; see the LICENSE file at the repository root.
Built from ["How to build a network stack in Ruby"](https://medium.com/geckoboard-under-the-hood/how-to-build-a-network-stack-in-ruby-f73aeb1b661b)
by the Geckoboard engineering team.
