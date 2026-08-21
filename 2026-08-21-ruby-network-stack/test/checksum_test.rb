require_relative 'test_helper'

include NetworkStack

test 'a header checksummed by us validates against itself' do
  header = [0x4500, 0x0028, 0x1c46, 0x4000, 0x4006, 0x0000, 0xac10, 0x0a63, 0xac10, 0x0a0c].pack('n*')
  checksum = Checksum.internet_checksum(header)
  header[10, 2] = [checksum].pack('n')
  assert Checksum.valid?(header)
end

test 'flipping a single bit after checksumming breaks validation' do
  header = [0x4500, 0x0028, 0x1c46, 0x4000, 0x4006, 0x0000, 0xac10, 0x0a63, 0xac10, 0x0a0c].pack('n*')
  checksum = Checksum.internet_checksum(header)
  header[10, 2] = [checksum].pack('n')
  corrupted = header.dup
  corrupted.setbyte(8, corrupted.getbyte(8) ^ 0x01)
  assert !Checksum.valid?(corrupted)
end

test 'odd-length input is zero-padded, not truncated' do
  # Three bytes: the last one is padded with a zero low byte before summing.
  three = [0x00, 0x01, 0xff].pack('C*')
  four = [0x00, 0x01, 0xff, 0x00].pack('C*')
  assert_equal Checksum.internet_checksum(four), Checksum.internet_checksum(three)
end

test 'carries out of the top 16 bits wrap back in (end-around carry)' do
  # 0xffff + 0xffff overflows 16 bits twice; the wrapped result must still fold to a single 16-bit sum.
  data = [0xffff, 0xffff].pack('n*')
  sum = Checksum.internet_checksum(data)
  assert sum >= 0 && sum <= 0xffff
  assert_equal 0x0000, sum # ~(0xffff + 0xffff carried down to 0xffff) == 0
end

test 'pseudo_header packs to exactly 12 bytes' do
  ph = Checksum.pseudo_header(0x0a000001, 0x0a000002, 6, 20)
  assert_equal 12, ph.bytesize
  assert_equal [0x0a000001, 0x0a000002, 0, 6, 20], ph.unpack('NNCCn')
end
