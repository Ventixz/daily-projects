module NetworkStack
  # Ethernet II framing: 6-byte destination MAC, 6-byte source MAC, a 2-byte
  # EtherType telling the next layer what's inside, then the payload. No
  # trailing FCS here -- pcap captures normally strip it before recording.
  class Ethernet < Struct.new(:dst_mac, :src_mac, :ethertype, :payload)
    ETHERTYPE_IPV4 = 0x0800

    def self.parse(bytes)
      raise ArgumentError, "frame too short for an Ethernet header (#{bytes.bytesize} bytes)" if bytes.bytesize < 14

      dst, src, ethertype = bytes[0, 14].unpack('a6a6n')
      new(format_mac(dst), format_mac(src), ethertype, bytes[14..])
    end

    def to_bytes
      [parse_mac(dst_mac), parse_mac(src_mac), ethertype].pack('a6a6n') + payload
    end

    def self.format_mac(raw6)
      raw6.unpack('C6').map { |b| format('%02x', b) }.join(':')
    end

    def self.parse_mac(str)
      str.split(':').map { |h| Integer(h, 16) }.pack('C6')
    end

    def parse_mac(str) = self.class.parse_mac(str)
  end
end
