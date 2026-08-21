require_relative 'checksum'

module NetworkStack
  # UDP header: source port, destination port, length (header + payload,
  # in bytes), checksum. No sequencing, no acknowledgement, no retransmit --
  # that's the entire point of the protocol.
  class UDP < Struct.new(:src_port, :dst_port, :payload)
    HEADER_BYTES = 8

    def self.parse(bytes)
      raise ArgumentError, "segment too short for a UDP header (#{bytes.bytesize} bytes)" if bytes.bytesize < HEADER_BYTES

      src_port, dst_port, length, _checksum = bytes[0, HEADER_BYTES].unpack('nnnn')
      new(src_port, dst_port, bytes[HEADER_BYTES, length - HEADER_BYTES])
    end

    # Checksum covers the pseudo-header (source/dest IP + protocol, supplied
    # by the IPv4 layer, which is the only layer that knows them) plus this
    # segment. Verifying is the same computation with the received checksum
    # field left in place: RFC 1071's fold-to-0xffff invariant.
    def to_bytes(pseudo_header)
      length = HEADER_BYTES + payload.bytesize
      segment = [src_port, dst_port, length, 0].pack('nnnn') + payload
      checksum = Checksum.internet_checksum(pseudo_header + segment)
      checksum = 0xffff if checksum.zero? # 0 means "no checksum" in UDP; 0xffff and 0 are the same sum
      segment[6, 2] = [checksum].pack('n')
      segment
    end
  end
end
