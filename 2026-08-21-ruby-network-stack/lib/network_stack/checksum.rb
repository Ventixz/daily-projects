module NetworkStack
  # RFC 1071 Internet checksum: 16-bit one's-complement sum of 16-bit words,
  # folded until it fits in 16 bits, then one's-complemented.
  module Checksum
    module_function

    def internet_checksum(bytes)
      bytes = bytes.dup
      bytes << "\x00".b if bytes.bytesize.odd?

      sum = bytes.unpack('n*').reduce(0, :+)
      sum = (sum & 0xffff) + (sum >> 16) while sum > 0xffff
      (~sum) & 0xffff
    end

    # IPv4/TCP/UDP checksums are computed over the header with the checksum
    # field itself zeroed, then the ones'-complemented sum is written into
    # that field. Adding that value back into the same sum always yields
    # 0xffff (a + ~a, in 16-bit ones'-complement), so recomputing the
    # checksum over the header *as received* -- checksum field included --
    # one's-complements 0xffff down to 0: that's the verification.
    def valid?(bytes)
      internet_checksum(bytes).zero?
    end

    # 12-byte pseudo-header TCP/UDP fold into their checksum: source IP,
    # destination IP, a zero byte, the protocol number, and the length of
    # the segment being checksummed (header + payload, not the pseudo-header
    # itself). Neither TCP nor UDP includes the IP addresses in their own
    # header, so without this the checksum couldn't catch a segment
    # misdelivered to the wrong host.
    def pseudo_header(src_ip, dst_ip, protocol, segment_length)
      [src_ip, dst_ip, 0, protocol, segment_length].pack('NNCCn')
    end
  end
end
