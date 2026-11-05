import java.util.*;

/** A tiny host: answers ARP requests for its IP and learns mappings into a cache with TTL. */
public final class ArpHost {
    public static final byte[] BROADCAST = EthernetFrame.parseMac("ff:ff:ff:ff:ff:ff");
    public static final byte[] ZERO_MAC = new byte[6];

    private final byte[] mac, ip;
    private final long ttlMillis;
    private final Map<String, Entry> cache = new HashMap<>();

    private record Entry(String mac, long learnedAt) {}

    public ArpHost(String mac, String ip, long ttlMillis) {
        this.mac = EthernetFrame.parseMac(mac);
        this.ip = ArpPacket.ip(ip);
        this.ttlMillis = ttlMillis;
    }

    /** Builds a broadcast who-has frame for targetIp. */
    public byte[] request(String targetIp) {
        ArpPacket p = new ArpPacket(ArpPacket.REQUEST, mac, ip, ZERO_MAC, ArpPacket.ip(targetIp));
        return new EthernetFrame(BROADCAST, mac, EthernetFrame.ETH_P_ARP, p.toBytes()).toBytes();
    }

    /** Handles an incoming frame at time nowMillis; returns a reply frame or null. */
    public byte[] receive(byte[] raw, long nowMillis) {
        EthernetFrame f;
        ArpPacket a;
        try {
            f = EthernetFrame.parse(raw);
            if (f.ethertype != EthernetFrame.ETH_P_ARP) return null;
            a = ArpPacket.parse(f.payload);
        } catch (IllegalArgumentException e) {
            return null; // malformed: drop silently
        }
        boolean forMe = Arrays.equals(a.targetIp, ip);
        boolean addressedToUs = Arrays.equals(f.dst, mac) || Arrays.equals(f.dst, BROADCAST);
        if (!addressedToUs) return null;
        // RFC 826: learn the sender if we already have it, or if the packet is for us.
        String key = ArpPacket.ipStr(a.senderIp);
        if (cache.containsKey(key) || forMe) cache.put(key, new Entry(EthernetFrame.mac(a.senderMac), nowMillis));
        if (a.opcode == ArpPacket.REQUEST && forMe) {
            ArpPacket reply = new ArpPacket(ArpPacket.REPLY, mac, ip, a.senderMac, a.senderIp);
            return new EthernetFrame(a.senderMac, mac, EthernetFrame.ETH_P_ARP, reply.toBytes()).toBytes();
        }
        return null;
    }

    public Optional<String> lookup(String ipAddr, long nowMillis) {
        Entry e = cache.get(ipAddr);
        if (e == null) return Optional.empty();
        if (nowMillis - e.learnedAt() > ttlMillis) { cache.remove(ipAddr); return Optional.empty(); }
        return Optional.of(e.mac());
    }
}
