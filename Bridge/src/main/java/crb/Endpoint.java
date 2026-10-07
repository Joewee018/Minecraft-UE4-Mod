package crb;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.SecureRandom;
import java.util.HexFormat;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Bounded loopback endpoint. One controlling peer at a time; a newly authenticated peer replaces the old one.
 * Producers (Minecraft threads) never block: every lane is either a bounded queue (offer) or a latest-value slot.
 * Lanes are independent, so a slow/failed texture transfer can never starve pose or state frames.
 */
public final class Endpoint {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    public static final int LANE_STATE = 0, LANE_POSE = 1, LANE_LIGHTMAP = 2, LANE_PARTICLES = 3, LANE_ICONS = 4, LANES = 5;

    public interface Listener {
        void onConnected(int connectionId);
        void onDisconnected(int connectionId, String reason);
        void onCommand(int connectionId, JsonObject command);
    }

    public record Input(JsonObject json, long receivedNanos, long seq) {}

    private final Listener listener;
    private final String token;
    private final String session;
    private ServerSocket server;
    private Thread acceptThread;
    private volatile boolean running;
    private volatile Peer current;
    private int nextConnectionId = 1;
    private final AtomicReference<Input> latestInput = new AtomicReference<>();
    private final AtomicLong seq = new AtomicLong(1);
    public final AtomicLong framesOut = new AtomicLong(), bytesOut = new AtomicLong(), framesIn = new AtomicLong();
    public final AtomicLong rejectedIn = new AtomicLong(), droppedOut = new AtomicLong(), oversizeOut = new AtomicLong();
    private final String welcomeJson;

    public Endpoint(Listener listener, String welcomeJson) {
        this.listener = listener;
        byte[] t = new byte[32];
        new SecureRandom().nextBytes(t);
        this.token = HexFormat.of().formatHex(t);
        this.session = HexFormat.of().formatHex(new SecureRandom().generateSeed(8));
        this.welcomeJson = welcomeJson;
    }

    public String session() { return session; }

    public void start(Path endpointFile, String bridgeVersion) throws IOException {
        server = new ServerSocket();
        server.setReuseAddress(false);
        server.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0), 2);
        running = true;
        JsonObject e = new JsonObject();
        e.addProperty("protocol", Wire.PROTOCOL);
        e.addProperty("port", server.getLocalPort());
        e.addProperty("token", token);
        e.addProperty("session", session);
        e.addProperty("bridge", bridgeVersion);
        e.addProperty("pid", ProcessHandle.current().pid());
        Files.createDirectories(endpointFile.toAbsolutePath().getParent());
        Path tmp = endpointFile.resolveSibling(endpointFile.getFileName() + ".tmp");
        Files.writeString(tmp, e.toString(), StandardCharsets.UTF_8);
        Files.move(tmp, endpointFile, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        acceptThread = new Thread(this::acceptLoop, "crb-accept");
        acceptThread.setDaemon(true);
        acceptThread.start();
        LOG.info("Crossover-Rebuilt bridge listening on 127.0.0.1:{} (protocol {})", server.getLocalPort(), Wire.PROTOCOL);
    }

    public void stop(Path endpointFile) {
        running = false;
        try { if (server != null) server.close(); } catch (IOException ignored) { }
        Peer p = current;
        if (p != null) p.close("bridge stopping");
        try { Files.deleteIfExists(endpointFile); } catch (IOException ignored) { }
    }

    public boolean connected() { Peer p = current; return p != null && p.open; }
    public int connectionId() { Peer p = current; return p == null ? 0 : p.id; }

    /** Latest input from the host, or null. */
    public Input input() { return latestInput.get(); }

    // ---- producer API (any thread, never blocks) ----
    public boolean control(int type, String json, byte[] bin) {
        Peer p = current;
        if (p == null || !p.open) return false;
        byte[] f = encode(type, json, bin);
        if (f == null) return false;
        if (!p.control.offer(f)) { droppedOut.incrementAndGet(); return false; }
        return true;
    }

    public void latest(int lane, int type, String json, byte[] bin) {
        Peer p = current;
        if (p == null || !p.open) return;
        byte[] f = encode(type, json, bin);
        if (f != null) p.latest[lane].set(f);
    }

    public boolean world(int type, String json, byte[] bin) {
        Peer p = current;
        if (p == null || !p.open) return false;
        byte[] f = encode(type, json, bin);
        if (f == null) return false;
        if (!p.world.offer(f)) { droppedOut.incrementAndGet(); return false; }
        return true;
    }

    public int worldFree() { Peer p = current; return p == null ? 0 : p.world.remainingCapacity(); }

    public boolean texture(String json, byte[] bin) {
        Peer p = current;
        if (p == null || !p.open) return false;
        byte[] f = encode(Wire.TEXTURE, json, bin);
        if (f == null) return false;
        return p.textures.offer(f);
    }

    public int textureFree() { Peer p = current; return p == null ? 0 : p.textures.remainingCapacity(); }

    private byte[] encode(int type, String json, byte[] bin) {
        try {
            return Wire.encode(type, seq.getAndIncrement(), json, bin);
        } catch (IllegalArgumentException ex) {
            oversizeOut.incrementAndGet();
            LOG.warn("Dropped oversize outgoing frame type {}: {}", type, ex.getMessage());
            return null;
        }
    }

    // ---- connection handling ----
    private void acceptLoop() {
        while (running) {
            try {
                Socket s = server.accept();
                if (!s.getInetAddress().isLoopbackAddress()) { s.close(); continue; }
                Thread t = new Thread(() -> handshake(s), "crb-handshake");
                t.setDaemon(true);
                t.start();
            } catch (IOException ex) {
                if (running) LOG.warn("accept failed: {}", ex.toString());
            }
        }
    }

    private void handshake(Socket s) {
        try {
            s.setTcpNoDelay(true);
            s.setSoTimeout(5000);
            s.setSendBufferSize(1 << 20);
            BufferedInputStream in = new BufferedInputStream(s.getInputStream(), 1 << 16);
            Wire.Frame hello = Wire.read(in);
            if (hello.type() != Wire.HELLO) throw new IOException("expected HELLO");
            JsonObject h = JsonParser.parseString(hello.json()).getAsJsonObject();
            if (!h.has("token") || !Wire.tokenEquals(h.get("token").getAsString(), token)) throw new IOException("bad token");
            if (!h.has("protocol") || h.get("protocol").getAsInt() != Wire.PROTOCOL) throw new IOException("protocol mismatch");
            s.setSoTimeout(0); // blocking reads after the handshake; close() unblocks them
            Peer p;
            synchronized (this) {
                Peer old = current;
                if (old != null) old.close("replaced by a new host connection");
                p = new Peer(s, in, nextConnectionId++);
                p.control.offer(Wire.encode(Wire.WELCOME, seq.getAndIncrement(), welcomeJson, null));
                current = p;
            }
            p.start();
            LOG.info("Unreal host connected (connection {})", p.id);
            listener.onConnected(p.id);
        } catch (Exception ex) {
            rejectedIn.incrementAndGet();
            LOG.warn("Rejected host connection: {}", ex.toString());
            try { s.close(); } catch (IOException ignored) { }
        }
    }

    private final class Peer {
        final Socket socket;
        final BufferedInputStream in;
        final int id;
        volatile boolean open = true;
        final BlockingQueue<byte[]> control = new ArrayBlockingQueue<>(256);
        final BlockingQueue<byte[]> world = new ArrayBlockingQueue<>(1024);
        final BlockingQueue<byte[]> textures = new ArrayBlockingQueue<>(8);
        @SuppressWarnings("unchecked")
        final AtomicReference<byte[]>[] latest = new AtomicReference[LANES];
        Thread reader, writer;

        Peer(Socket socket, BufferedInputStream in, int id) {
            this.socket = socket; this.in = in; this.id = id;
            for (int i = 0; i < LANES; i++) latest[i] = new AtomicReference<>();
        }

        void start() {
            reader = new Thread(this::readLoop, "crb-reader-" + id);
            writer = new Thread(this::writeLoop, "crb-writer-" + id);
            reader.setDaemon(true); writer.setDaemon(true);
            reader.start(); writer.start();
        }

        void close(String reason) {
            synchronized (this) {
                if (!open) return;
                open = false;
            }
            try { socket.close(); } catch (IOException ignored) { }
            synchronized (Endpoint.this) { if (current == this) current = null; }
            latestInput.set(null);
            LOG.info("Unreal host disconnected (connection {}): {}", id, reason);
            listener.onDisconnected(id, reason);
        }

        void readLoop() {
            try {
                while (open) {
                    Wire.Frame f = Wire.read(in);
                    framesIn.incrementAndGet();
                    switch (f.type()) {
                        case Wire.INPUT -> {
                            JsonObject j = JsonParser.parseString(f.json()).getAsJsonObject();
                            latestInput.set(new Input(j, System.nanoTime(), f.seq()));
                        }
                        case Wire.COMMAND -> {
                            if (f.json().length() > 16384) { rejectedIn.incrementAndGet(); continue; }
                            listener.onCommand(id, JsonParser.parseString(f.json()).getAsJsonObject());
                        }
                        case Wire.BYE -> { close("host said goodbye"); return; }
                        default -> rejectedIn.incrementAndGet();
                    }
                }
            } catch (Exception ex) {
                close(ex.getClass().getSimpleName() + ": " + ex.getMessage());
            }
        }

        void writeLoop() {
            try (OutputStream out = new BufferedOutputStream(socket.getOutputStream(), 1 << 17)) {
                while (open) {
                    boolean wrote = false;
                    byte[] f;
                    while ((f = control.poll()) != null) { write(out, f); wrote = true; }
                    for (int lane = 0; lane < LANES; lane++) {
                        f = latest[lane].getAndSet(null);
                        if (f != null) { write(out, f); wrote = true; }
                    }
                    for (int i = 0; i < 48 && (f = world.poll()) != null; i++) { write(out, f); wrote = true; }
                    // One texture chunk per pass: large textures interleave with live frames.
                    if ((f = textures.poll()) != null) { write(out, f); wrote = true; }
                    if (wrote) out.flush();
                    else Thread.sleep(2);
                }
            } catch (Exception ex) {
                close("write failed: " + ex.getMessage());
            }
        }

        void write(OutputStream out, byte[] f) throws IOException {
            out.write(f);
            framesOut.incrementAndGet();
            bytesOut.addAndGet(f.length);
        }
    }
}
