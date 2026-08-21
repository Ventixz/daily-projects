require_relative 'ethernet'
require_relative 'ip'
require_relative 'tcp'
require_relative 'udp'
require_relative 'checksum'

module NetworkStack
  # Walks a raw Ethernet frame down through IPv4 into TCP/UDP, the way a
  # protocol analyzer does: each layer only knows how to parse its own
  # header and hands the rest of the bytes to whoever's declared next
  # (EtherType, then IP protocol number).
  module Decoder
    Decoded = Struct.new(:ethernet, :ip, :transport, :ip_checksum_valid, :transport_checksum_valid)

    def self.decode(raw_frame)
      eth = Ethernet.parse(raw_frame)
      return Decoded.new(eth, nil, nil, nil, nil) unless eth.ethertype == Ethernet::ETHERTYPE_IPV4

      ip_bytes = eth.payload
      ip = IPv4.parse(ip_bytes)
      ip_checksum_valid = Checksum.valid?(ip_bytes[0, IPv4::HEADER_BYTES])

      transport = nil
      transport_checksum_valid = nil
      pseudo = ip.pseudo_header(ip.payload.bytesize)

      case ip.protocol
      when IPv4::PROTO_TCP
        transport = TCP.parse(ip.payload)
        transport_checksum_valid = Checksum.valid?(pseudo + ip.payload)
      when IPv4::PROTO_UDP
        transport = UDP.parse(ip.payload)
        transport_checksum_valid = Checksum.valid?(pseudo + ip.payload)
      end

      Decoded.new(eth, ip, transport, ip_checksum_valid, transport_checksum_valid)
    end

    def self.summarize(decoded)
      return "ethertype 0x#{decoded.ethernet.ethertype.to_s(16)} (not IPv4)" unless decoded.ip

      ip = decoded.ip
      case decoded.transport
      when TCP
        t = decoded.transport
        "IP #{ip.src_ip}.#{t.src_port} > #{ip.dst_ip}.#{t.dst_port}: " \
          "Flags [#{t.flag_summary}], seq #{t.seq}, ack #{t.ack}, win #{t.window}, length #{t.payload.bytesize}"
      when UDP
        t = decoded.transport
        "IP #{ip.src_ip}.#{t.src_port} > #{ip.dst_ip}.#{t.dst_port}: UDP, length #{t.payload.bytesize}"
      else
        "IP #{ip.src_ip} > #{ip.dst_ip}: proto #{ip.protocol}, length #{ip.payload.bytesize}"
      end
    end
  end
end
