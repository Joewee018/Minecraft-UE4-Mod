package crb;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.BufferedInputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.HexFormat;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Live Java-side bridge test against a running Minecraft 1.20.1 dev client (disposable superflat world).
 * Acts as a minimal host: no Unreal involved. Usage: LiveProbe <endpoint.json> <report.json> <bridge.jar>
 */
public final class LiveProbe {
    private Socket socket;
    private OutputStream out;
    private long seq = 1;
    private final AtomicReference<JsonObject> state = new AtomicReference<>();
    private final AtomicReference<JsonObject> pose = new AtomicReference<>();
    private final ConcurrentLinkedQueue<JsonObject> results = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<JsonObject> events = new ConcurrentLinkedQueue<>();
    private volatile JsonObject welcome;
    private volatile long poseFrames, poseBytes, textureChunks, sections, models, lightmaps, particleBatches;
    private volatile String readerError = "";
    private final JsonArray checks = new JsonArray();
    private final JsonArray failures = new JsonArray();
    private final JsonObject metrics = new JsonObject();
    private long attackPresses, usePresses;

    public static void main(String[] args) throws Exception {
        LiveProbe p = new LiveProbe();
        JsonObject report = new JsonObject();
        report.addProperty("startedUtc", Instant.now().toString());
        report.addProperty("bridgeJar", args.length > 2 ? args[2] : "");
        report.addProperty("bridgeJarSha256", args.length > 2 ? sha256(Path.of(args[2])) : "");
        boolean ok;
        try {
            ok = p.run(Path.of(args[0]));
        } catch (Throwable t) {
            p.fail("exception: " + t);
            ok = false;
        }
        report.addProperty("passed", ok && p.failures.size() == 0);
        report.add("welcome", p.welcome);
        report.add("checks", p.checks);
        report.add("failures", p.failures);
        report.add("metrics", p.metrics);
        report.addProperty("finishedUtc", Instant.now().toString());
        Files.writeString(Path.of(args[1]), new GsonBuilder().setPrettyPrinting().create().toJson(report));
        System.out.println("LiveProbe passed=" + report.get("passed").getAsBoolean() + " checks=" + p.checks.size() + " failures=" + p.failures.size());
        for (var f : p.failures) System.out.println("FAIL: " + f.getAsString());
        System.exit(report.get("passed").getAsBoolean() ? 0 : 1);
    }

    static String sha256(Path p) throws Exception {
        if (!Files.exists(p)) return "missing";
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(p))).toUpperCase();
    }

    void pass(String c) { checks.add(c); System.out.println("PASS: " + c); }
    void fail(String c) { failures.add(c); System.out.println("FAIL: " + c); }
    boolean check(boolean cond, String c) { if (cond) pass(c); else fail(c); return cond; }

    void connect(Path endpoint) throws Exception {
        JsonObject e = JsonParser.parseString(Files.readString(endpoint)).getAsJsonObject();
        socket = new Socket(InetAddress.getLoopbackAddress(), e.get("port").getAsInt());
        socket.setTcpNoDelay(true);
        out = socket.getOutputStream();
        JsonObject h = new JsonObject();
        h.addProperty("token", e.get("token").getAsString());
        h.addProperty("protocol", Wire.PROTOCOL);
        send(Wire.HELLO, h);
        BufferedInputStream in = new BufferedInputStream(socket.getInputStream(), 1 << 16);
        Thread t = new Thread(() -> {
            try {
                while (true) {
                    Wire.Frame f = Wire.read(in);
                    switch (f.type()) {
                        case Wire.WELCOME -> welcome = JsonParser.parseString(f.json()).getAsJsonObject();
                        case Wire.STATE -> state.set(JsonParser.parseString(f.json()).getAsJsonObject());
                        case Wire.POSE -> { pose.set(JsonParser.parseString(f.json()).getAsJsonObject()); poseFrames++; poseBytes += f.bin().length; }
                        case Wire.RESULT -> results.add(JsonParser.parseString(f.json()).getAsJsonObject());
                        case Wire.EVENT -> events.add(JsonParser.parseString(f.json()).getAsJsonObject());
                        case Wire.TEXTURE -> textureChunks++;
                        case Wire.SECTION -> sections++;
                        case Wire.MODEL -> models++;
                        case Wire.LIGHTMAP -> lightmaps++;
                        case Wire.PARTICLES -> particleBatches++;
                        default -> { }
                    }
                }
            } catch (Exception ex) { readerError = ex.toString(); }
        }, "probe-reader");
        t.setDaemon(true);
        t.start();
    }

    synchronized void send(int type, JsonObject j) throws Exception {
        out.write(Wire.encode(type, seq++, j.toString(), null));
        out.flush();
    }

    JsonObject command(String op, JsonObject args, long timeoutMs) throws Exception {
        String id = op + "-" + seq;
        JsonObject c = new JsonObject();
        c.addProperty("id", id); c.addProperty("op", op); c.add("args", args == null ? new JsonObject() : args);
        send(Wire.COMMAND, c);
        long end = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < end) {
            for (JsonObject r : results) if (r.has("id") && r.get("id").getAsString().equals(id)) { results.remove(r); return r; }
            Thread.sleep(10);
        }
        JsonObject r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", "timeout"); return r;
    }

    void input(double fwd, float yaw, float pitch, boolean jump, boolean attack, boolean use, int slot) throws Exception {
        JsonObject j = new JsonObject();
        j.addProperty("fwd", fwd); j.addProperty("strafe", 0); j.addProperty("jump", jump);
        j.addProperty("sneak", false); j.addProperty("sprint", false);
        j.addProperty("attack", attack); j.addProperty("use", use);
        j.addProperty("attackPresses", attackPresses); j.addProperty("usePresses", usePresses);
        j.addProperty("yaw", yaw); j.addProperty("pitch", pitch); j.addProperty("lease", 350);
        if (slot >= 0) j.addProperty("slot", slot);
        send(Wire.INPUT, j);
    }

    void hold(double fwd, float yaw, float pitch, long ms) throws Exception {
        long end = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < end) { input(fwd, yaw, pitch, false, false, false, -1); Thread.sleep(50); }
    }

    JsonObject waitState(long ms) throws Exception {
        long end = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < end) { JsonObject s = state.get(); if (s != null) return s; Thread.sleep(20); }
        return null;
    }

    /** Movement + animation regression: Java position, pose sequence and gait must all advance. */
    boolean movementCheck(String label) throws Exception {
        JsonObject r = command("player.reset", null, 3000);
        hold(0, 0, 0, 400);
        JsonObject s0 = state.get(); JsonObject p0 = pose.get();
        long frames0 = poseFrames;
        hold(1.0, 0, 0, 1500);
        JsonObject s1 = state.get(); JsonObject p1 = pose.get();
        hold(0, 0, 0, 300);
        double dz = s1.get("z").getAsDouble() - s0.get("z").getAsDouble();
        long dseq = p1.get("seq").getAsLong() - p0.get("seq").getAsLong();
        double gait = Math.abs(p1.get("walkPos").getAsDouble() - p0.get("walkPos").getAsDouble());
        boolean ok = r.get("ok").getAsBoolean() && dz > 2.0 && dseq >= 20 && gait > 1.0 && poseFrames - frames0 >= 20
            && s1.get("inputThread").getAsString().equals("Render thread");
        metrics.addProperty("move." + label, String.format("dz=%.2f poseSeq+%d gait+%.2f frames+%d thread=%s", dz, dseq, gait, poseFrames - frames0, s1.get("inputThread").getAsString()));
        return check(ok, label + ": input moved Java player (dz=" + String.format("%.2f", dz) + "), pose frames advanced (+" + dseq + "), gait advanced (" + String.format("%.2f", gait) + ") on the client thread");
    }

    boolean run(Path endpoint) throws Exception {
        connect(endpoint);
        JsonObject s = waitState(240000); // first launch creates the superflat world
        if (!check(welcome != null && s != null, "Connected, authenticated and received WELCOME + STATE")) return false;
        check(welcome.get("minecraft").getAsString().equals("1.20.1"), "Runtime reports Minecraft 1.20.1 (" + welcome.get("minecraft").getAsString() + "), loader " + welcome.get("loader").getAsString() + ", Fabric API " + welcome.get("fabricApi").getAsString());
        JsonObject info = command("world.info", null, 3000);
        check(info.get("superflat").getAsBoolean() && info.get("disposable").getAsBoolean(), "World is the disposable superflat test world (" + info.get("levelName").getAsString() + ", " + info.get("generator").getAsString() + ")");
        check(info.get("thread").getAsString().equals("Server thread"), "World commands executed on the integrated server thread");
        command("time.set", obj("time", 6000), 2000);
        command("fixture.off", null, 2000);
        Thread.sleep(1500);
        check(sections > 0 && models > 0, "Section and block-model frames streamed (" + sections + " sections, " + models + " models)");
        check(lightmaps > 0, "Live 16x16 light map exported");
        movementCheck("baseline");

        JsonObject list = command("fixture.list", null, 2000);
        for (var fe : list.getAsJsonArray("fixtures")) {
            String id = fe.getAsJsonObject().get("id").getAsString();
            JsonObject on = command("fixture.toggle", obj("id", id), 5000);
            check(on.get("ok").getAsBoolean() && id.equals(on.get("active").getAsString()), "Fixture '" + id + "' turned on: " + on.get("message").getAsString());
            hold(0, 0, 0, 600);
            movementCheck("fixture " + id + " on");
            JsonObject off = command("fixture.toggle", obj("id", id), 5000);
            check(off.get("ok").getAsBoolean() && "".equals(off.get("active").getAsString()), "Fixture '" + id + "' turned off by selecting it again");
            movementCheck("fixture " + id + " off");
        }
        JsonObject st = state.get();
        check(st.get("poseErrors").getAsLong() == 0, "No pose capture errors (" + st.get("poseErrors").getAsLong() + ")");
        metrics.addProperty("textureFailures", st.get("textureFailures").getAsLong());
        metrics.addProperty("textureLastError", st.get("textureLastError").getAsString());
        metrics.addProperty("texturesSent", st.get("texturesSent").getAsLong());
        check(textureChunks > 0 && st.get("texturesSent").getAsLong() > 0, "Textures streamed in bounded chunks (" + textureChunks + " chunks)");

        // Gravity gun: Java-authoritative block removal, hold, distance and placement.
        command("fixture.toggle", obj("id", "shapes"), 5000);
        command("player.reset", null, 2000);
        // Planks at (2,-60,3): aim from eye (0.5,-58.38,0.5).
        double dx = 2.5 - 0.5, dy = -59.5 - (-60 + 1.62), dz = 3.5 - 0.5;
        float yaw = (float) Math.toDegrees(Math.atan2(-dx, dz));
        float pitch = (float) -Math.toDegrees(Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)));
        hold(0, yaw, pitch, 500);
        JsonObject grab = command("gravity.grab", null, 3000);
        hold(0, yaw, pitch, 300);
        check(grab.get("ok").getAsBoolean() && state.get().getAsJsonObject("gravity").get("holding").getAsBoolean(), "Gravity gun grabbed a real block on the server: " + grab.get("message").getAsString());
        JsonObject dist = command("gravity.distance", obj("delta", 2), 2000);
        check(dist.get("ok").getAsBoolean(), "Gravity gun distance changed (" + (dist.has("distance") ? dist.get("distance").getAsDouble() : -1) + ")");
        hold(0, yaw + 40, pitch, 400);
        JsonObject rel = command("gravity.release", null, 3000);
        hold(0, 0, 0, 300);
        check(rel.get("ok").getAsBoolean() && !state.get().getAsJsonObject("gravity").get("holding").getAsBoolean(), "Gravity gun released and Java placed the block: " + rel.get("message").getAsString());
        command("fixture.toggle", obj("id", "shapes"), 5000);

        // Game mode: survival <-> creative through vanilla ServerPlayer.setGameMode (synced to the client).
        JsonObject gmc = command("gamemode.set", obj("mode", "creative"), 3000);
        hold(0, 0, 0, 400);
        check(gmc.get("ok").getAsBoolean() && "creative".equals(state.get().get("gamemode").getAsString()), "Game mode switched to creative on the server and the client (" + gmc.get("message").getAsString() + ")");
        JsonObject gms = command("gamemode.set", obj("mode", "survival"), 3000);
        hold(0, 0, 0, 400);
        check(gms.get("ok").getAsBoolean() && "survival".equals(state.get().get("gamemode").getAsString()), "Game mode switched back to survival");

        // Herobrine (From The Fog, dev mods folder): the mod's own admin function builds its armor-stand rig,
        // which must reach Unreal through the entity pose group.
        JsonObject hs = command("herobrine.status", null, 3000);
        if (hs.get("installed").getAsBoolean()) {
            JsonObject sum = command("herobrine.summon", obj("kind", "fake"), 4000);
            hold(0, 0, 0, 800);
            JsonObject after = command("herobrine.status", null, 3000);
            int stands = 0;
            for (var e : pose.get().getAsJsonArray("entities")) if (e.getAsJsonObject().get("type").getAsString().equals("minecraft:armor_stand")) stands++;
            check(sum.get("ok").getAsBoolean() && after.get("herobrineEntities").getAsInt() > 0 && stands > 0,
                "From The Fog summoned Herobrine (" + after.get("herobrineEntities").getAsInt() + " rig entities) and the pose stream carries " + stands + " armor stands");
            JsonObject clr = command("herobrine.clear", null, 4000);
            hold(0, 0, 0, 500);
            check(command("herobrine.status", null, 3000).get("herobrineEntities").getAsInt() == 0, "Herobrine removed: " + clr.get("message").getAsString());
        } else check(false, "From The Fog (Herobrine) is not installed in Work\\mc\\mods - run Tools\\InstallMods.ps1");
        movementCheck("after game mode and Herobrine");

        // Item use / attack through vanilla input paths.
        command("fixture.toggle", obj("id", "items"), 5000);
        hold(0, 0, 0, 300);
        for (int i = 0; i < 6; i++) { input(0, 0, 0, false, false, false, 3); Thread.sleep(50); }
        check("minecraft:apple".equals(pose.get().get("mainhand").getAsString()), "Hotbar selection reached Java (main hand = " + pose.get().get("mainhand").getAsString() + ")");
        usePresses++;
        long useEnd = System.currentTimeMillis() + 600; boolean using = false;
        // Empty main hand (slot 9) + shield in the off hand: holding use raises the shield through vanilla useItem.
        while (System.currentTimeMillis() < useEnd) { input(0, 0, 0, false, false, true, 8); Thread.sleep(50); using |= pose.get().get("using").getAsBoolean(); }
        input(0, 0, 0, false, false, false, 8);
        check(using, "Holding use started Java item use (off-hand shield) and the pose exported it");
        command("fixture.toggle", obj("id", "items"), 5000);
        movementCheck("final");

        metrics.addProperty("poseFrames", poseFrames);
        metrics.addProperty("avgPoseBytes", poseFrames == 0 ? 0 : poseBytes / poseFrames);
        metrics.addProperty("particleBatches", particleBatches);
        metrics.addProperty("readerError", readerError);
        socket.close();
        // Reconnect: the bridge must accept a fresh host.
        Thread.sleep(500);
        state.set(null); welcome = null;
        connect(endpoint);
        check(waitState(8000) != null && welcome != null, "Reconnected after disconnect and resumed STATE");
        hold(0, 0, 0, 300);
        socket.close();
        return failures.size() == 0;
    }

    static JsonObject obj(String k, Object v) {
        JsonObject o = new JsonObject();
        if (v instanceof Number n) o.addProperty(k, n); else o.addProperty(k, String.valueOf(v));
        return o;
    }
}
