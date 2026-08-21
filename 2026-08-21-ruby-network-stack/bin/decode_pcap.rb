#!/usr/bin/env ruby
# Minimal tcpdump -n: decode every frame in a pcap file and print one
# summary line per packet.

require_relative '../lib/network_stack'

path = ARGV[0] or abort "usage: #{$PROGRAM_NAME} <path-to.pcap>"

NetworkStack::PcapFile.each_packet(path).with_index(1) do |packet, n|
  decoded = NetworkStack::Decoder.decode(packet.raw)
  flags = []
  flags << 'BAD IP CKSUM' if decoded.ip_checksum_valid == false
  flags << 'BAD TRANSPORT CKSUM' if decoded.transport_checksum_valid == false
  suffix = flags.empty? ? '' : " [#{flags.join(', ')}]"
  puts format('%2d %02d:%02d:%02d.%06d %s%s', n, packet.ts_sec / 3600 % 24, packet.ts_sec / 60 % 60,
              packet.ts_sec % 60, packet.ts_usec, NetworkStack::Decoder.summarize(decoded), suffix)
end
