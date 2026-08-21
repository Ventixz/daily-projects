require_relative 'test_helper'

include NetworkStack

test 'round-trips MACs and ethertype through build then parse' do
  frame = Ethernet.new('aa:bb:cc:dd:ee:ff', '11:22:33:44:55:66', Ethernet::ETHERTYPE_IPV4, 'payload-bytes')
  parsed = Ethernet.parse(frame.to_bytes)

  assert_equal 'aa:bb:cc:dd:ee:ff', parsed.dst_mac
  assert_equal '11:22:33:44:55:66', parsed.src_mac
  assert_equal Ethernet::ETHERTYPE_IPV4, parsed.ethertype
  assert_equal 'payload-bytes', parsed.payload
end

test 'header is exactly 14 bytes before the payload starts' do
  frame = Ethernet.new('00:00:00:00:00:01', '00:00:00:00:00:02', 0x0800, 'X')
  bytes = frame.to_bytes
  assert_equal 15, bytes.bytesize # 14-byte header + 1-byte payload
  assert_equal 'X', bytes[14..]
end

test 'rejects a frame shorter than a full Ethernet header' do
  begin
    Ethernet.parse('short')
    assert false, 'expected ArgumentError'
  rescue ArgumentError
    assert true
  end
end
