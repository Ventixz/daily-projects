import java.util.Arrays;

/** Ethernet II frame: dst(6) src(6) ethertype(2) payload. */
public final class EthernetFrame {
    public static final int ETH_P_ARP = 0x0806;
    public static final int ETH_P_IPV4 = 0x0800;
    public static final int HEADER_LEN = 14;

    public final byte[] dst, src, payload;
    public final int ethertype;

    public EthernetFrame(byte[] dst, byte[] src, int ethertype, byte[] payload) {
        if (dst.length != 6 || src.length != 6) throw new IllegalArgumentException("MAC must be 6 bytes");
        this.dst = dst.clone();
        this.src = src.clone();
        this.ethertype = ethertype & 0xFFFF;
        this.payload = payload.clone();
    }

    public static EthernetFrame parse(byte[] buf) {
        if (buf.length < HEADER_LEN) throw new IllegalArgumentException("frame too short: " + buf.length);
        int type = ((buf[12] & 0xFF) << 8) | (buf[13] & 0xFF);
        return new EthernetFrame(Arrays.copyOfRange(buf, 0, 6), Arrays.copyOfRange(buf, 6, 12),
                type, Arrays.copyOfRange(buf, HEADER_LEN, buf.length));
    }

    public byte[] toBytes() {
        byte[] out = new byte[HEADER_LEN + payload.length];
        System.arraycopy(dst, 0, out, 0, 6);
        System.arraycopy(src, 0, out, 6, 6);
        out[12] = (byte) (ethertype >> 8);
        out[13] = (byte) ethertype;
        System.arraycopy(payload, 0, out, HEADER_LEN, payload.length);
        return out;
    }

    public static String mac(byte[] m) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < m.length; i++) sb.append(i > 0 ? ":" : "").append(String.format("%02x", m[i] & 0xFF));
        return sb.toString();
    }

    public static byte[] parseMac(String s) {
        String[] p = s.split(":");
        if (p.length != 6) throw new IllegalArgumentException("bad MAC: " + s);
        byte[] b = new byte[6];
        for (int i = 0; i < 6; i++) b[i] = (byte) Integer.parseInt(p[i], 16);
        return b;
    }
}
