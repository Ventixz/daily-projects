require_relative 'test_helper'

include NetworkStack

def udp_pseudo(src, dst, payload)
  Checksum.pseudo_header(IPv4.ip_to_n(src), IPv4.ip_to_n(dst), IPv4::PROTO_UDP, UDP::HEADER_BYTES + payload.bytesize)
end

test 'round-trips fields through build then parse' do
  datagram = UDP.new(53_000, 53, 'query-bytes')
  pseudo = udp_pseudo('10.0.0.1', '10.0.0.2', 'query-bytes')
  parsed = UDP.parse(datagram.to_bytes(pseudo))

  assert_equal 53_000, parsed.src_port
  assert_equal 53, parsed.dst_port
  assert_equal 'query-bytes', parsed.payload
end

test 'length field is header plus payload, not payload alone' do
  bytes = UDP.new(1, 2, 'abcde').to_bytes(udp_pseudo('10.0.0.1', '10.0.0.2', 'abcde'))
  length = bytes[4, 2].unpack1('n')
  assert_equal UDP::HEADER_BYTES + 5, length
end

test 'a checksum that would compute to zero is transmitted as all-ones instead' do
  # RFC 768: an all-zero checksum field means "no checksum" -- a genuine
  # zero result gets remapped to 0xffff so it's never confused with that.
  # Crafted so the ones'-complement sum -- all-zero pseudo-header, an 8-byte
  # header with ports/checksum zero and length 10 (header + these 2 payload
  # bytes), plus the payload word 0xfff5 -- lands on exactly 0xffff, which
  # complements to a real zero result: 10 + 0xfff5 == 0xffff, no carry.
  datagram = UDP.new(0, 0, [0xff, 0xf5].pack('C2'))
  pseudo = "\x00".b * 12
  bytes = datagram.to_bytes(pseudo)

  # Confirm the setup actually hits the zero case, not just assert the remap.
  length = UDP::HEADER_BYTES + datagram.payload.bytesize
  unremapped = Checksum.internet_checksum([0, 0, length, 0].pack('nnnn') + datagram.payload)
  assert_equal 0, unremapped

  transmitted = bytes[6, 2].unpack1('n')
  assert_equal 0xffff, transmitted
end
