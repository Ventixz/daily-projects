require_relative 'test_helper'

include NetworkStack

SAMPLE_PCAP = File.join(__dir__, '..', 'resources', 'sample.pcap')

test 'the bundled sample capture decodes cleanly, all five packets, no checksum failures' do
  assert File.exist?(SAMPLE_PCAP), 'run resources/generate_sample_pcap.rb first'

  packets = PcapFile.each_packet(SAMPLE_PCAP).to_a
  assert_equal 5, packets.size

  packets.each do |packet|
    decoded = Decoder.decode(packet.raw)
    assert decoded.ip, 'every sample packet is IPv4'
    assert decoded.ip_checksum_valid, "IP checksum failed for a packet from #{decoded.ip.src_ip}"
    assert decoded.transport_checksum_valid, "transport checksum failed for a packet from #{decoded.ip.src_ip}"
  end
end

test 'the sample capture tells the story of a handshake, a request, and a UDP query' do
  decoded = PcapFile.each_packet(SAMPLE_PCAP).map { |p| Decoder.decode(p.raw) }

  assert_equal ['S', 'S.', '.', 'P.'], decoded.first(4).map { |d| d.transport.flag_summary }
  assert decoded[3].transport.payload.include?('GET / HTTP/1.1')
  assert_equal UDP, decoded[4].transport.class
  assert_equal 53, decoded[4].transport.dst_port
end

test 'bin/decode_pcap.rb runs end to end against the sample capture and prints no failures' do
  script = File.join(__dir__, '..', 'bin', 'decode_pcap.rb')
  output = `ruby #{script} #{SAMPLE_PCAP}`
  assert $?.success?, "decode_pcap.rb exited #{$?.exitstatus}"
  assert_equal 5, output.lines.size
  assert !output.include?('BAD'), "unexpected checksum failure in:\n#{output}"
  assert output.include?('UDP, length 30')
end
