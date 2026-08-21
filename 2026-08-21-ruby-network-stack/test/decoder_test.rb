require_relative 'test_helper'

include NetworkStack

def build_tcp_frame(src_ip, dst_ip, tcp)
  pseudo = Checksum.pseudo_header(IPv4.ip_to_n(src_ip), IPv4.ip_to_n(dst_ip), IPv4::PROTO_TCP, TCP::HEADER_BYTES + tcp.payload.bytesize)
  ip = IPv4.new(0, 0, 1, 0, 0, 64, IPv4::PROTO_TCP, src_ip, dst_ip, tcp.to_bytes(pseudo))
  Ethernet.new('aa:aa:aa:aa:aa:02', 'aa:aa:aa:aa:aa:01', Ethernet::ETHERTYPE_IPV4, ip.to_bytes).to_bytes
end

test 'decodes a well-formed TCP-over-IPv4-over-Ethernet frame end to end' do
  tcp = TCP.new(51_000, 80, 100, 0, TCP::SYN, 64_240, '')
  frame = build_tcp_frame('10.0.0.1', '10.0.0.2', tcp)

  decoded = Decoder.decode(frame)
  assert_equal '10.0.0.1', decoded.ip.src_ip
  assert_equal '10.0.0.2', decoded.ip.dst_ip
  assert_equal 51_000, decoded.transport.src_port
  assert decoded.transport.syn?
  assert decoded.ip_checksum_valid
  assert decoded.transport_checksum_valid
end

test 'flags a tampered payload as a transport checksum failure without crashing' do
  tcp = TCP.new(1, 2, 0, 0, TCP::ACK, 1000, 'original')
  frame = build_tcp_frame('10.0.0.1', '10.0.0.2', tcp)
  frame[-1] = 'X' # corrupt the last payload byte after checksumming

  decoded = Decoder.decode(frame)
  assert decoded.ip_checksum_valid # IP header itself untouched
  assert !decoded.transport_checksum_valid
end

test 'a non-IPv4 ethertype decodes to no IP/transport layer' do
  frame = Ethernet.new('aa:aa:aa:aa:aa:02', 'aa:aa:aa:aa:aa:01', 0x0806, 'arp-ish-bytes').to_bytes
  decoded = Decoder.decode(frame)
  assert_equal nil, decoded.ip
  assert Decoder.summarize(decoded).include?('0x806')
end

test 'summarize renders a recognizable tcpdump-style line for TCP' do
  tcp = TCP.new(51_000, 80, 100, 0, TCP::SYN, 64_240, '')
  decoded = Decoder.decode(build_tcp_frame('10.0.0.1', '10.0.0.2', tcp))
  line = Decoder.summarize(decoded)
  assert line.include?('10.0.0.1.51000 > 10.0.0.2.80')
  assert line.include?('Flags [S]')
end
