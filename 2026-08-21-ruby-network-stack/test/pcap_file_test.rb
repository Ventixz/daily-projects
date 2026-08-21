require_relative 'test_helper'
require 'tmpdir'

include NetworkStack

test 'writes and reads back packets with their timestamps and bytes intact' do
  packets = [
    PcapFile::Packet.new(1_700_000_000, 0, 'first-frame'),
    PcapFile::Packet.new(1_700_000_001, 500_000, 'second-frame-bytes'),
  ]

  Dir.mktmpdir do |dir|
    path = File.join(dir, 'test.pcap')
    PcapFile.write(path, packets)

    read_back = PcapFile.each_packet(path).to_a
    assert_equal 2, read_back.size
    assert_equal [1_700_000_000, 0, 'first-frame'], [read_back[0].ts_sec, read_back[0].ts_usec, read_back[0].raw]
    assert_equal [1_700_000_001, 500_000, 'second-frame-bytes'], [read_back[1].ts_sec, read_back[1].ts_usec, read_back[1].raw]
  end
end

test 'an empty capture yields zero packets, not an error' do
  Dir.mktmpdir do |dir|
    path = File.join(dir, 'empty.pcap')
    PcapFile.write(path, [])
    assert_equal [], PcapFile.each_packet(path).to_a
  end
end

test 'rejects a file whose magic number is not the little-endian pcap magic' do
  Dir.mktmpdir do |dir|
    path = File.join(dir, 'bogus.pcap')
    File.binwrite(path, "\x00\x00\x00\x00" + "\x00" * 20)
    begin
      PcapFile.each_packet(path).to_a
      assert false, 'expected ArgumentError'
    rescue ArgumentError
      assert true
    end
  end
end
