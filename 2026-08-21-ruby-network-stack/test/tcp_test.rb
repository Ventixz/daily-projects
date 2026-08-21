require_relative 'test_helper'

include NetworkStack

def pseudo_for(src, dst, payload)
  Checksum.pseudo_header(IPv4.ip_to_n(src), IPv4.ip_to_n(dst), IPv4::PROTO_TCP, TCP::HEADER_BYTES + payload.bytesize)
end

test 'round-trips fields through build then parse' do
  segment = TCP.new(51_000, 80, 1000, 2000, TCP::SYN | TCP::ACK, 64_240, 'body')
  pseudo = pseudo_for('10.0.0.1', '10.0.0.2', 'body')
  parsed = TCP.parse(segment.to_bytes(pseudo))

  assert_equal 51_000, parsed.src_port
  assert_equal 80, parsed.dst_port
  assert_equal 1000, parsed.seq
  assert_equal 2000, parsed.ack
  assert_equal 64_240, parsed.window
  assert_equal 'body', parsed.payload
  assert parsed.syn?
  assert parsed.ack?
  assert !parsed.fin?
end

test 'checksum covers the pseudo-header, so the same segment differs by source IP' do
  segment = TCP.new(1, 2, 0, 0, TCP::ACK, 1000, 'x')
  bytes_a = segment.to_bytes(pseudo_for('10.0.0.1', '10.0.0.2', 'x'))
  bytes_b = segment.to_bytes(pseudo_for('10.0.0.9', '10.0.0.2', 'x'))
  checksum_a = bytes_a[16, 2].unpack1('n')
  checksum_b = bytes_b[16, 2].unpack1('n')
  assert checksum_a != checksum_b
end

test 'flag_summary matches tcpdump-style single letters' do
  assert_equal 'S', TCP.new(0, 0, 0, 0, TCP::SYN, 0, '').flag_summary
  assert_equal 'S.', TCP.new(0, 0, 0, 0, TCP::SYN | TCP::ACK, 0, '').flag_summary
  assert_equal '.', TCP.new(0, 0, 0, 0, TCP::ACK, 0, '').flag_summary
  assert_equal 'P.', TCP.new(0, 0, 0, 0, TCP::PSH | TCP::ACK, 0, '').flag_summary
  assert_equal 'F.', TCP.new(0, 0, 0, 0, TCP::FIN | TCP::ACK, 0, '').flag_summary
end

test 'rejects a segment shorter than a full TCP header' do
  begin
    TCP.parse('short')
    assert false, 'expected ArgumentError'
  rescue ArgumentError
    assert true
  end
end
