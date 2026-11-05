import java.util.Arrays;

public class Tests {
    static int run = 0;
    static void check(boolean c, String name) {
        run++;
        if (!c) throw new AssertionError("FAIL: " + name);
        System.out.println("ok - " + name);
    }

    public static void main(String[] x) {
        byte[] m1 = EthernetFrame.parseMac("02:00:00:00:00:0a");
        byte[] m2 = EthernetFrame.parseMac("02:00:00:00:00:0b");

        EthernetFrame f = new EthernetFrame(m1, m2, 0x0800, new byte[]{1, 2, 3});
        EthernetFrame g = EthernetFrame.parse(f.toBytes());
        check(Arrays.equals(g.dst, m1) && Arrays.equals(g.src, m2) && g.ethertype == 0x0800
                && Arrays.equals(g.payload, new byte[]{1, 2, 3}), "frame round-trips");
        check(f.toBytes()[12] == 0x08 && f.toBytes()[13] == 0x00, "ethertype is big-endian");
        check(EthernetFrame.mac(m1).equals("02:00:00:00:00:0a"), "mac formatting");

        boolean threw = false;
        try { EthernetFrame.parse(new byte[10]); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "short frame rejected");

        ArpPacket p = new ArpPacket(ArpPacket.REQUEST, m1, ArpPacket.ip("10.0.0.1"), new byte[6], ArpPacket.ip("10.0.0.2"));
        byte[] pb = p.toBytes();
        check(pb.length == 28 && pb[7] == 1, "ARP request is 28 bytes, opcode 1");
        ArpPacket q = ArpPacket.parse(pb);
        check(q.opcode == 1 && ArpPacket.ipStr(q.targetIp).equals("10.0.0.2"), "ARP round-trips");

        byte[] bad = pb.clone(); bad[1] = 6;
        threw = false;
        try { ArpPacket.parse(bad); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "non-Ethernet hardware type rejected");

        threw = false;
        try { ArpPacket.ip("10.0.0.300"); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "bad IP rejected");

        ArpHost a = new ArpHost("02:00:00:00:00:0a", "10.0.0.1", 1000);
        ArpHost b = new ArpHost("02:00:00:00:00:0b", "10.0.0.2", 1000);
        byte[] req = a.request("10.0.0.2");
        EthernetFrame rf = EthernetFrame.parse(req);
        check(Arrays.equals(rf.dst, ArpHost.BROADCAST) && rf.ethertype == EthernetFrame.ETH_P_ARP, "request is broadcast ARP");

        byte[] rep = b.receive(req, 0);
        check(rep != null, "target replies");
        ArpPacket rp = ArpPacket.parse(EthernetFrame.parse(rep).payload);
        check(rp.opcode == ArpPacket.REPLY && Arrays.equals(rp.senderMac, m2), "reply carries target MAC");
        check(Arrays.equals(EthernetFrame.parse(rep).dst, m1), "reply is unicast to requester");
        check(b.lookup("10.0.0.1", 1).orElse("").equals("02:00:00:00:00:0a"), "responder learns requester");

        check(a.receive(rep, 5) == null, "reply produces no further reply");
        check(a.lookup("10.0.0.2", 10).orElse("").equals("02:00:00:00:00:0b"), "requester caches answer");
        check(a.lookup("10.0.0.2", 2000).isEmpty(), "cache entry expires after TTL");

        ArpHost c = new ArpHost("02:00:00:00:00:0c", "10.0.0.3", 1000);
        check(c.receive(req, 0) == null, "request for another IP ignored");
        check(c.lookup("10.0.0.1", 1).isEmpty(), "unrelated sender not cached");
        check(c.receive(new byte[]{1, 2, 3}, 0) == null, "garbage dropped");

        System.out.println(run + " checks passed");
    }
}
