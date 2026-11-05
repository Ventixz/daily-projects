import java.util.Arrays;

/** ARP packet for Ethernet/IPv4 (28 bytes). */
public final class ArpPacket {
    public static final int REQUEST = 1, REPLY = 2, LEN = 28;

    public final int opcode;
    public final byte[] senderMac, senderIp, targetMac, targetIp;

    public ArpPacket(int opcode, byte[] senderMac, byte[] senderIp, byte[] targetMac, byte[] targetIp) {
        this.opcode = opcode;
        this.senderMac = senderMac.clone();
        this.senderIp = senderIp.clone();
        this.targetMac = targetMac.clone();
        this.targetIp = targetIp.clone();
    }

    public static ArpPacket parse(byte[] b) {
        if (b.length < LEN) throw new IllegalArgumentException("ARP too short: " + b.length);
        int htype = u16(b, 0), ptype = u16(b, 2);
        if (htype != 1 || ptype != EthernetFrame.ETH_P_IPV4 || b[4] != 6 || b[5] != 4)
            throw new IllegalArgumentException("unsupported ARP hardware/protocol");
        int op = u16(b, 6);
        if (op != REQUEST && op != REPLY) throw new IllegalArgumentException("bad opcode " + op);
        return new ArpPacket(op, Arrays.copyOfRange(b, 8, 14), Arrays.copyOfRange(b, 14, 18),
                Arrays.copyOfRange(b, 18, 24), Arrays.copyOfRange(b, 24, 28));
    }

    public byte[] toBytes() {
        byte[] b = new byte[LEN];
        b[1] = 1;                                  // htype = Ethernet
        b[2] = 0x08; b[3] = 0x00;                  // ptype = IPv4
        b[4] = 6; b[5] = 4;                        // hlen, plen
        b[6] = (byte) (opcode >> 8); b[7] = (byte) opcode;
        System.arraycopy(senderMac, 0, b, 8, 6);
        System.arraycopy(senderIp, 0, b, 14, 4);
        System.arraycopy(targetMac, 0, b, 18, 6);
        System.arraycopy(targetIp, 0, b, 24, 4);
        return b;
    }

    public static byte[] ip(String s) {
        String[] p = s.split("\\.");
        if (p.length != 4) throw new IllegalArgumentException("bad IP: " + s);
        byte[] b = new byte[4];
        for (int i = 0; i < 4; i++) {
            int v = Integer.parseInt(p[i]);
            if (v < 0 || v > 255) throw new IllegalArgumentException("bad IP: " + s);
            b[i] = (byte) v;
        }
        return b;
    }

    public static String ipStr(byte[] b) {
        return (b[0] & 0xFF) + "." + (b[1] & 0xFF) + "." + (b[2] & 0xFF) + "." + (b[3] & 0xFF);
    }

    private static int u16(byte[] b, int o) { return ((b[o] & 0xFF) << 8) | (b[o + 1] & 0xFF); }
}
