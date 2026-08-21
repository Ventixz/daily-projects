require_relative 'checksum'

module NetworkStack
  # IPv4 header, options-free (IHL fixed at 5 32-bit words == 20 bytes).
  class IPv4 < Struct.new(:dscp, :ecn, :id, :flags, :fragment_offset, :ttl,
                           :protocol, :src_ip, :dst_ip, :payload)
    PROTO_TCP = 6
    PROTO_UDP = 17
    HEADER_BYTES = 20
    VERSION = 4
    IHL_WORDS = 5

    def self.parse(bytes)
      raise ArgumentError, "packet too short for an IPv4 header (#{bytes.bytesize} bytes)" if bytes.bytesize < HEADER_BYTES

      version_ihl, dscp_ecn, total_length, id, flags_frag, ttl, protocol,
        checksum, src_ip, dst_ip = bytes[0, HEADER_BYTES].unpack('CCnnnCCnNN')

      version = version_ihl >> 4
      ihl = version_ihl & 0x0f
      raise ArgumentError, "not an IPv4 header (version=#{version})" unless version == VERSION
      raise ArgumentError, "IHL #{ihl} != #{IHL_WORDS} (options are not supported)" unless ihl == IHL_WORDS

      header = bytes[0, HEADER_BYTES]
      unless Checksum.valid?(header)
        warn "IPv4 header checksum mismatch (got 0x#{checksum.to_s(16)})"
      end

      new(dscp_ecn >> 2, dscp_ecn & 0x03, id, flags_frag >> 13, flags_frag & 0x1fff,
          ttl, protocol, n_to_ip(src_ip), n_to_ip(dst_ip), bytes[HEADER_BYTES, total_length - HEADER_BYTES])
    end

    def to_bytes
      version_ihl = (VERSION << 4) | IHL_WORDS
      dscp_ecn = (dscp << 2) | ecn
      flags_frag = (flags << 13) | fragment_offset
      total_length = HEADER_BYTES + payload.bytesize

      header = [version_ihl, dscp_ecn, total_length, id, flags_frag, ttl, protocol,
                0, self.class.ip_to_n(src_ip), self.class.ip_to_n(dst_ip)].pack('CCnnnCCnNN')
      checksum = Checksum.internet_checksum(header)
      header[10, 2] = [checksum].pack('n')
      header + payload
    end

    def pseudo_header(segment_length)
      Checksum.pseudo_header(self.class.ip_to_n(src_ip), self.class.ip_to_n(dst_ip), protocol, segment_length)
    end

    def self.n_to_ip(n) = [n].pack('N').unpack('C4').join('.')
    def self.ip_to_n(str) = str.split('.').map(&:to_i).pack('C4').unpack1('N')
  end
end
