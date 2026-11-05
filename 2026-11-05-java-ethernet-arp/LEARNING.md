# Learning: Ethernet & ARP (Java)

**Source:** "Let's Code a TCP/IP Stack, Part 1: Ethernet & ARP" in the Network programming
section of [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: the tutorial is in C and uses a Linux TAP device; the sandbox has none, so this is my own Java version that works on
in-memory byte arrays: an Ethernet II codec, an ARP codec, and a small host that answers requests and keeps an ARP cache.

Run: `javac -d out *.java && java -cp out Main`. Test: `java -cp out Tests` (19 checks, no dependencies).

## Layout

- `EthernetFrame.java` — parse/serialise dst MAC, src MAC, ethertype, payload; MAC helpers.
- `ArpPacket.java` — 28-byte Ethernet/IPv4 ARP packet, with validation of hardware/protocol types and opcode.
- `ArpHost.java` — builds broadcast who-has requests, replies to requests for its IP, learns senders, expires cache entries by TTL.
- `Main.java` — two hosts resolving each other. `Tests.java` — round-trips, malformed input, reply logic, cache expiry.

## What I learned

- **Network byte order is big-endian**, and Java's signed bytes mean every field needs `& 0xFF` before shifting.
- **ARP is the glue between layers 3 and 2.** A broadcast "who has IP X?" gets a unicast "X is at MAC Y".
- **RFC 826 learning rule:** update an existing cache entry from any ARP packet, but only create a new one when the packet is addressed to you. That avoids caching every host on the segment.
- **Parsers should drop bad input quietly.** Wrong lengths, unknown hardware types and bad opcodes are rejected, and the host ignores them instead of crashing.
- **Keeping time as an argument** (`nowMillis`) made TTL expiry testable without sleeping.

## Known limits

- No real interface: nothing is sent on a wire, and there is no TAP device.
- No pending-request queue, retries or gratuitous ARP. IPv4, ICMP and TCP (later tutorial parts) are not implemented.
