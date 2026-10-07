package crb;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/** Growable little-endian binary writer with a hard capacity limit. */
public final class Bin {
    private ByteBuffer buf;
    private final int limit;

    public Bin(int initial, int limit) {
        this.limit = limit;
        this.buf = ByteBuffer.allocate(Math.min(initial, limit)).order(ByteOrder.LITTLE_ENDIAN);
    }

    private void ensure(int n) {
        if (buf.remaining() >= n) return;
        int need = buf.position() + n;
        if (need > limit) throw new IllegalStateException("binary payload would exceed " + limit + " bytes");
        int cap = Math.min(limit, Math.max(need, buf.capacity() * 2));
        ByteBuffer nb = ByteBuffer.allocate(cap).order(ByteOrder.LITTLE_ENDIAN);
        buf.flip();
        nb.put(buf);
        buf = nb;
    }

    public boolean fits(int n) { return buf.position() + n <= limit; }
    public Bin f32(float v) { ensure(4); buf.putFloat(v); return this; }
    public Bin u32(int v) { ensure(4); buf.putInt(v); return this; }
    public Bin u16(int v) { ensure(2); buf.putShort((short) v); return this; }
    public Bin u8(int v) { ensure(1); buf.put((byte) v); return this; }
    public Bin bytes(byte[] b, int off, int len) { ensure(len); buf.put(b, off, len); return this; }
    public int size() { return buf.position(); }
    public byte[] toArray() { return Arrays.copyOf(buf.array(), buf.position()); }
}
