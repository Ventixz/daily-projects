#!/usr/bin/env ruby
# Builds resources/sample.pcap from this project's own Ethernet/IPv4/TCP/UDP
# builders -- a three-way handshake plus one data segment, and a DNS-shaped
# UDP query. Round-tripping through the library's own writer is what lets
# the test suite and `bin/decode_pcap.rb` exercise real captured-looking
# bytes without needing a live network capture.

require_relative '../lib/network_stack'
include NetworkStack

CLIENT_MAC = 'aa:aa:aa:aa:aa:01'
SERVER_MAC = 'aa:aa:aa:aa:aa:02'
CLIENT_IP = '10.0.0.1'
SERVER_IP = '10.0.0.2'

def ip_frame(src_mac, dst_mac, ip)
  Ethernet.new(dst_mac, src_mac, Ethernet::ETHERTYPE_IPV4, ip.to_bytes).to_bytes
end

def tcp_ip(src_ip, dst_ip, tcp)
  pseudo = Checksum.pseudo_header(IPv4.ip_to_n(src_ip), IPv4.ip_to_n(dst_ip), IPv4::PROTO_TCP, TCP::HEADER_BYTES + tcp.payload.bytesize)
  IPv4.new(0, 0, rand(0xffff), 0, 0, 64, IPv4::PROTO_TCP, src_ip, dst_ip, tcp.to_bytes(pseudo))
end

def udp_ip(src_ip, dst_ip, udp)
  pseudo = Checksum.pseudo_header(IPv4.ip_to_n(src_ip), IPv4.ip_to_n(dst_ip), IPv4::PROTO_UDP, UDP::HEADER_BYTES + udp.payload.bytesize)
  IPv4.new(0, 0, rand(0xffff), 0, 0, 64, IPv4::PROTO_UDP, src_ip, dst_ip, udp.to_bytes(pseudo))
end

packets = []
t = 1_700_000_000

# Three-way handshake: SYN, SYN-ACK, ACK.
syn = TCP.new(51_000, 80, 1000, 0, TCP::SYN, 64_240, '')
packets << PcapFile::Packet.new(t, 0, ip_frame(CLIENT_MAC, SERVER_MAC, tcp_ip(CLIENT_IP, SERVER_IP, syn)))

syn_ack = TCP.new(80, 51_000, 5000, 1001, TCP::SYN | TCP::ACK, 65_160, '')
packets << PcapFile::Packet.new(t, 200, ip_frame(SERVER_MAC, CLIENT_MAC, tcp_ip(SERVER_IP, CLIENT_IP, syn_ack)))

ack = TCP.new(51_000, 80, 1001, 5001, TCP::ACK, 64_240, '')
packets << PcapFile::Packet.new(t, 400, ip_frame(CLIENT_MAC, SERVER_MAC, tcp_ip(CLIENT_IP, SERVER_IP, ack)))

# A data segment carrying an HTTP request.
request = "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n"
data = TCP.new(51_000, 80, 1001, 5001, TCP::PSH | TCP::ACK, 64_240, request)
packets << PcapFile::Packet.new(t, 600, ip_frame(CLIENT_MAC, SERVER_MAC, tcp_ip(CLIENT_IP, SERVER_IP, data)))

# A UDP query, shaped like a DNS lookup.
query = UDP.new(53_211, 53, "\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00".b + "\x07example\x04test\x00\x00\x01\x00\x01".b)
packets << PcapFile::Packet.new(t + 1, 0, ip_frame(CLIENT_MAC, SERVER_MAC, udp_ip(CLIENT_IP, SERVER_IP, query)))

out = File.join(__dir__, 'sample.pcap')
PcapFile.write(out, packets)
puts "wrote #{packets.size} packets to #{out}"
