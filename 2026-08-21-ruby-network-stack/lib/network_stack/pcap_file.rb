module NetworkStack
  # Minimal reader/writer for the classic libpcap capture format (the .pcap
  # tcpdump/Wireshark produce with `-w`, not the newer pcapng). Little-endian
  # only -- that's what every x86 capture tool writes.
  module PcapFile
    MAGIC_LITTLE_ENDIAN = 0xa1b2c3d4
    GLOBAL_HEADER_BYTES = 24
    RECORD_HEADER_BYTES = 16
    LINKTYPE_ETHERNET = 1

    Packet = Struct.new(:ts_sec, :ts_usec, :raw)

    def self.write(path, packets, linktype: LINKTYPE_ETHERNET)
      File.open(path, 'wb') do |f|
        f.write [MAGIC_LITTLE_ENDIAN, 2, 4, 0, 0, 65_535, linktype].pack('VvvVVVV')
        packets.each do |packet|
          f.write [packet.ts_sec, packet.ts_usec, packet.raw.bytesize, packet.raw.bytesize].pack('VVVV')
          f.write packet.raw
        end
      end
    end

    def self.each_packet(path)
      return to_enum(:each_packet, path) unless block_given?

      File.open(path, 'rb') do |f|
        header = f.read(GLOBAL_HEADER_BYTES) or raise ArgumentError, 'file too short for a pcap global header'
        magic, = header.unpack('V')
        raise ArgumentError, format('not a little-endian pcap file (magic 0x%08x)', magic) unless magic == MAGIC_LITTLE_ENDIAN

        loop do
          record_header = f.read(RECORD_HEADER_BYTES)
          break if record_header.nil?
          raise ArgumentError, 'truncated packet record header' if record_header.bytesize < RECORD_HEADER_BYTES

          ts_sec, ts_usec, incl_len, = record_header.unpack('VVVV')
          raw = f.read(incl_len)
          raise ArgumentError, 'truncated packet body' if raw.nil? || raw.bytesize < incl_len

          yield Packet.new(ts_sec, ts_usec, raw)
        end
      end
    end
  end
end
