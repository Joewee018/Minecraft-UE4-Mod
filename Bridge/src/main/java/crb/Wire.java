package crb;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * Crossover-Rebuilt frame format (little endian), shared with Unreal's CrbProtocol.h:
 * u32 magic "CRB1", u16 type, u16 flags, u32 seq, u32 jsonLen, u32 binLen, json bytes, binary bytes.
 * Pure Java: no Minecraft classes, so it is unit tested without a game.
 */
public final class Wire {
    public static final int MAGIC = 0x31425243;
    public static final int PROTOCOL = 1;
    public static final int HEADER = 20;
    public static final int MAX_JSON = 256 * 1024;
    public static final int MAX_BIN = 1024 * 1024;

    public static final int HELLO = 1, WELCOME = 2, BYE = 3;
    public static final int INPUT = 10, COMMAND = 11, RESULT = 12;
    public static final int STATE = 20, SECTION = 21, WINDOW = 22, MODEL = 23, TEXTURE = 24, POSE = 25;
    public static final int LIGHTMAP = 26, PARTICLES = 27, MODS = 28, EVENT = 30, ICONS = 31;

    private Wire() {}

    public record Frame(int type, int flags, long seq, String json, byte[] bin) {}

    public static final class ProtocolException extends IOException {
        public ProtocolException(String message) { super(message); }
    }

    public static byte[] encode(int type, long seq, String json, byte[] bin) {
        byte[] j = json == null ? new byte[0] : json.getBytes(StandardCharsets.UTF_8);
        int b = bin == null ? 0 : bin.length;
        if (j.length > MAX_JSON) throw new IllegalArgumentException("json payload " + j.length + " exceeds " + MAX_JSON);
        if (b > MAX_BIN) throw new IllegalArgumentException("binary payload " + b + " exceeds " + MAX_BIN);
        ByteBuffer out = ByteBuffer.allocate(HEADER + j.length + b).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(MAGIC).putShort((short) type).putShort((short) 0).putInt((int) seq).putInt(j.length).putInt(b);
        out.put(j);
        if (b > 0) out.put(bin);
        return out.array();
    }

    /** Reads one frame, enforcing limits before allocating payload buffers. */
    public static Frame read(InputStream in) throws IOException {
        byte[] h = new byte[HEADER];
        readFully(in, h);
        ByteBuffer hb = ByteBuffer.wrap(h).order(ByteOrder.LITTLE_ENDIAN);
        int magic = hb.getInt();
        if (magic != MAGIC) throw new ProtocolException("bad magic " + Integer.toHexString(magic));
        int type = hb.getShort() & 0xFFFF;
        int flags = hb.getShort() & 0xFFFF;
        long seq = hb.getInt() & 0xFFFFFFFFL;
        long jl = hb.getInt() & 0xFFFFFFFFL;
        long bl = hb.getInt() & 0xFFFFFFFFL;
        if (type == 0 || type > 64) throw new ProtocolException("unknown frame type " + type);
        if (jl > MAX_JSON) throw new ProtocolException("json length " + jl + " exceeds limit");
        if (bl > MAX_BIN) throw new ProtocolException("binary length " + bl + " exceeds limit");
        byte[] j = new byte[(int) jl];
        readFully(in, j);
        byte[] b = new byte[(int) bl];
        readFully(in, b);
        return new Frame(type, flags, seq, new String(j, StandardCharsets.UTF_8), b);
    }

    private static void readFully(InputStream in, byte[] buf) throws IOException {
        int off = 0;
        while (off < buf.length) {
            int n = in.read(buf, off, buf.length - off);
            if (n < 0) throw new EOFException("peer closed");
            off += n;
        }
    }

    /** Constant-time token comparison. */
    public static boolean tokenEquals(String a, String b) {
        if (a == null || b == null) return false;
        byte[] x = a.getBytes(StandardCharsets.US_ASCII), y = b.getBytes(StandardCharsets.US_ASCII);
        int diff = x.length ^ y.length;
        for (int i = 0; i < Math.min(x.length, y.length); i++) diff |= x[i] ^ y[i];
        return diff == 0;
    }
}
