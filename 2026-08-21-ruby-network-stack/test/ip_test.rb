require_relative 'test_helper'

include NetworkStack

def sample_ip(payload = 'hello')
  IPv4.new(0, 0, 0x1234, 0, 0, 64, IPv4::PROTO_TCP, '10.0.0.1', '10.0.0.2', payload)
end

test 'round-trips fields through build then parse' do
  parsed = IPv4.parse(sample_ip.to_bytes)
  assert_equal '10.0.0.1', parsed.src_ip
  assert_equal '10.0.0.2', parsed.dst_ip
  assert_equal IPv4::PROTO_TCP, parsed.protocol
  assert_equal 64, parsed.ttl
  assert_equal 0x1234, parsed.id
  assert_equal 'hello', parsed.payload
end

test 'total length in the header covers header plus payload exactly' do
  bytes = sample_ip('12345').to_bytes
  total_length = bytes[2, 2].unpack1('n')
  assert_equal IPv4::HEADER_BYTES + 5, total_length
  assert_equal total_length, bytes.bytesize
end

test 'to_bytes writes a self-consistent header checksum' do
  bytes = sample_ip.to_bytes
  assert Checksum.valid?(bytes[0, IPv4::HEADER_BYTES])
end

test 'a corrupted header fails checksum validation, but parse still returns it (warns, does not raise)' do
  bytes = sample_ip.to_bytes
  bytes.setbyte(8, bytes.getbyte(8) ^ 0xff) # flip the TTL byte after checksumming
  assert !Checksum.valid?(bytes[0, IPv4::HEADER_BYTES])

  original_stderr = $stderr
  $stderr = StringIO.new
  parsed = IPv4.parse(bytes)
  $stderr = original_stderr

  assert_equal 64 ^ 0xff, parsed.ttl
end

test 'ip_to_n and n_to_ip are inverses' do
  assert_equal '192.168.1.1', IPv4.n_to_ip(IPv4.ip_to_n('192.168.1.1'))
  assert_equal 0x0a000001, IPv4.ip_to_n(IPv4.n_to_ip(0x0a000001))
end

test 'rejects an IHL that implies options, which this parser does not support' do
  bytes = sample_ip.to_bytes
  bytes.setbyte(0, 0x46) # version 4, IHL 6 (24-byte header)
  begin
    IPv4.parse(bytes)
    assert false, 'expected ArgumentError'
  rescue ArgumentError
    assert true
  end
end
