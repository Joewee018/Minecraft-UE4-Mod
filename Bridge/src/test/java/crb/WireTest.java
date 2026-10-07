package crb;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.EOFException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.*;

class WireTest {
    @Test void roundTrip() throws Exception {
        byte[] bin = { 1, 2, 3, (byte) 250 };
        byte[] f = Wire.encode(Wire.POSE, 77, "{\"a\":1}", bin);
        assertEquals(Wire.HEADER + 7 + 4, f.length);
        Wire.Frame r = Wire.read(new ByteArrayInputStream(f));
        assertEquals(Wire.POSE, r.type());
        assertEquals(77, r.seq());
        assertEquals("{\"a\":1}", r.json());
        assertArrayEquals(bin, r.bin());
    }

    @Test void littleEndianHeaderMatchesUnreal() {
        byte[] f = Wire.encode(Wire.STATE, 0x01020304, "", null);
        ByteBuffer b = ByteBuffer.wrap(f).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(0x31425243, b.getInt());
        assertEquals(Wire.STATE, b.getShort());
        assertEquals(0, b.getShort());
        assertEquals(0x01020304, b.getInt());
        assertEquals((byte) 'C', f[0]); // "CRB1" on the wire
    }

    @Test void rejectsOversizeOutgoing() {
        assertThrows(IllegalArgumentException.class, () -> Wire.encode(Wire.TEXTURE, 1, "", new byte[Wire.MAX_BIN + 1]));
        assertThrows(IllegalArgumentException.class, () -> Wire.encode(Wire.STATE, 1, "x".repeat(Wire.MAX_JSON + 1), null));
    }

    @Test void rejectsMalformedIncoming() {
        byte[] f = Wire.encode(Wire.INPUT, 1, "{}", null);
        f[0] = 0; // bad magic
        assertThrows(Wire.ProtocolException.class, () -> Wire.read(new ByteArrayInputStream(f)));
        ByteBuffer huge = ByteBuffer.allocate(Wire.HEADER).order(ByteOrder.LITTLE_ENDIAN);
        huge.putInt(Wire.MAGIC).putShort((short) Wire.INPUT).putShort((short) 0).putInt(1).putInt(Wire.MAX_JSON + 1).putInt(0);
        assertThrows(Wire.ProtocolException.class, () -> Wire.read(new ByteArrayInputStream(huge.array())));
        ByteBuffer badType = ByteBuffer.allocate(Wire.HEADER).order(ByteOrder.LITTLE_ENDIAN);
        badType.putInt(Wire.MAGIC).putShort((short) 999).putShort((short) 0).putInt(1).putInt(0).putInt(0);
        assertThrows(Wire.ProtocolException.class, () -> Wire.read(new ByteArrayInputStream(badType.array())));
        byte[] truncated = java.util.Arrays.copyOf(Wire.encode(Wire.INPUT, 1, "{\"x\":1}", null), Wire.HEADER + 3);
        assertThrows(EOFException.class, () -> Wire.read(new ByteArrayInputStream(truncated)));
    }

    @Test void tokenComparison() {
        assertTrue(Wire.tokenEquals("abc", "abc"));
        assertFalse(Wire.tokenEquals("abc", "abd"));
        assertFalse(Wire.tokenEquals("abc", "abcd"));
        assertFalse(Wire.tokenEquals(null, "abc"));
    }

    @Test void binWriterEnforcesLimit() {
        Bin b = new Bin(4, 8);
        b.u32(1).u32(2);
        assertThrows(IllegalStateException.class, () -> b.u8(3));
        assertEquals(8, b.toArray().length);
    }
}
