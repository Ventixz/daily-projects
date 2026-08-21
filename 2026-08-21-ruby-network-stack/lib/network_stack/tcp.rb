require_relative 'checksum'

module NetworkStack
  # TCP header, options-free (data offset fixed at 5 32-bit words == 20
  # bytes). Flags are kept as a raw bitmask -- FIN/SYN/RST/PSH/ACK/URG in
  # that bit order, matching the wire -- with named predicates on top.
  class TCP < Struct.new(:src_port, :dst_port, :seq, :ack, :flags, :window, :payload)
    HEADER_BYTES = 20

    FIN = 0x01
    SYN = 0x02
    RST = 0x04
    PSH = 0x08
    ACK = 0x10
    URG = 0x20

    def self.parse(bytes)
      raise ArgumentError, "segment too short for a TCP header (#{bytes.bytesize} bytes)" if bytes.bytesize < HEADER_BYTES

      src_port, dst_port, seq, ack, offset_reserved, flags, window, _checksum, _urg =
        bytes[0, HEADER_BYTES].unpack('nnNNCCnnn')

      data_offset = (offset_reserved >> 4) * 4
      raise ArgumentError, "data offset #{data_offset} != #{HEADER_BYTES} (options are not supported)" unless data_offset == HEADER_BYTES

      new(src_port, dst_port, seq, ack, flags, window, bytes[HEADER_BYTES..])
    end

    def to_bytes(pseudo_header)
      data_offset_reserved = (HEADER_BYTES / 4) << 4
      segment = [src_port, dst_port, seq, ack, data_offset_reserved, flags, window, 0, 0].pack('nnNNCCnnn') + payload
      checksum = Checksum.internet_checksum(pseudo_header + segment)
      segment[16, 2] = [checksum].pack('n')
      segment
    end

    def fin? = flags.anybits?(FIN)
    def syn? = flags.anybits?(SYN)
    def rst? = flags.anybits?(RST)
    def psh? = flags.anybits?(PSH)
    def ack? = flags.anybits?(ACK)
    def urg? = flags.anybits?(URG)

    # tcpdump-style flag summary, e.g. "S" for a bare SYN, "P." for PSH+ACK.
    def flag_summary
      letters = { SYN => 'S', FIN => 'F', RST => 'R', PSH => 'P', ACK => '.', URG => 'U' }
      summary = letters.select { |bit, _| flags.anybits?(bit) }.values.join
      summary.empty? ? '.' : summary
    end
  end
end
