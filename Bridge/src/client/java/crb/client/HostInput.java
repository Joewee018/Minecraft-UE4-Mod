package crb.client;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.InputConstants;
import crb.Endpoint;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.util.Mth;

/**
 * Applies the Unreal host's input to Minecraft's own key mappings and rotation on the client (render) thread,
 * so vanilla KeyboardInput, LocalPlayer.aiStep and handleKeybinds run the authoritative movement/interaction code.
 */
public final class HostInput {
    public static final long MAX_LEASE_MS = 500;
    private static volatile boolean active;
    private static volatile boolean attackHeld;
    private long lastAttackPresses = -1, lastUsePresses = -1, lastSwapPresses = -1, lastDropPresses = -1;
    private boolean holding;
    public long appliedSeq, appliedCount, staleReleases;
    public double lastAgeMs;
    public String applyThread = "";
    public float appliedYaw, appliedPitch;

    public static boolean active() { return active; }
    /** True while SM64 Steve Movement drives the player (set each tick from the host input). */
    public static volatile boolean sm64Driving;
    /** True while Craft 64 owns the attack button (Unreal sends a "c64" input block). */
    public static volatile boolean c64Driving;
    /** True while Minecraft x Elden Combat owns the attack / use buttons (Unreal sends an "ec" input block). */
    public static volatile boolean ecDriving;

    /** Used by the Minecraft.continueAttack mixin: the host decides whether the attack key is held. */
    public static boolean overrideContinueAttack(boolean vanilla) {
        if (!active) return vanilla;
        Minecraft mc = Minecraft.getInstance();
        return attackHeld && mc.screen == null && !c64Driving && !ecDriving;
    }

    public void apply(Minecraft mc, Endpoint endpoint) {
        LocalPlayer player = mc.player;
        Endpoint.Input in = endpoint.connected() ? endpoint.input() : null;
        long ageMs = in == null ? Long.MAX_VALUE : (System.nanoTime() - in.receivedNanos()) / 1_000_000L;
        JsonObject j = in == null ? null : in.json();
        long lease = j != null && j.has("lease") ? Math.min(MAX_LEASE_MS, Math.max(50, j.get("lease").getAsLong())) : 350;
        if (player == null || j == null || ageMs > lease || mc.screen != null) {
            crb.client.sm64.Sm64Controller.INSTANCE.stale();
            crb.client.pp.PPController.INSTANCE.stale();
            if (c64Driving && mc.getSingleplayerServer() != null && player != null) { java.util.UUID id = player.getUUID(); mc.getSingleplayerServer().execute(() -> crb.c64.Craft64.INSTANCE.input(id, false, "", -1)); }
            if (ecDriving && mc.getSingleplayerServer() != null && player != null) { java.util.UUID id = player.getUUID(); mc.getSingleplayerServer().execute(() -> crb.ec.EldenCombat.INSTANCE.input(id, null)); }
            if (holding) { release(mc.options); staleReleases++; }
            active = false;
            return;
        }
        Options o = mc.options;
        float fwd = f(j, "fwd"), strafe = f(j, "strafe");
        // SM64 Steve Movement: the host sends the raw stick + camera yaw; Sm64Controller moves the player from
        // Player.travel, so vanilla's movement keys stay up (no vanilla jump, sneak edge-stop, sprint or auto-jump).
        crb.client.sm64.Sm64Controller sm = crb.client.sm64.Sm64Controller.INSTANCE;
        float camYaw = j.has("yaw") && Float.isFinite(j.get("yaw").getAsFloat()) ? j.get("yaw").getAsFloat() : player.getYRot();
        sm.input(b(j, "sm64"), fwd, strafe, camYaw, b(j, "jump"), b(j, "sneak"), b(j, "sprint"),
            j.has("jumpPresses") ? j.get("jumpPresses").getAsLong() : -1, j.has("sneakPresses") ? j.get("sneakPresses").getAsLong() : -1);
        // Physics & Portal mod: raw stick + camera yaw; the "pp" block carries the roll key and the portal gun counters.
        JsonObject ppk = j.has("pp") && j.get("pp").isJsonObject() ? j.getAsJsonObject("pp") : null;
        crb.client.pp.PPController.INSTANCE.input(ppk != null, fwd, strafe, camYaw, b(j, "jump"), b(j, "sprint"), ppk);
        sm64Driving = sm.wants(player) || crb.client.pp.PPController.INSTANCE.wants(player);
        o.keyUp.setDown(!sm64Driving && fwd > 0.3f);
        o.keyDown.setDown(!sm64Driving && fwd < -0.3f);
        o.keyLeft.setDown(!sm64Driving && strafe < -0.3f);
        o.keyRight.setDown(!sm64Driving && strafe > 0.3f);
        o.keyJump.setDown(!sm64Driving && b(j, "jump"));
        o.keyShift.setDown(!sm64Driving && b(j, "sneak"));
        o.keySprint.setDown(!sm64Driving && b(j, "sprint"));
        boolean attack = b(j, "attack"), use = b(j, "use");
        // Craft 64: the attack button fires the Doom weapon (server-side rules), so vanilla attack / mining stays up.
        JsonObject c64 = j.has("c64") && j.get("c64").isJsonObject() ? j.getAsJsonObject("c64") : null;
        c64Driving = c64 != null;
        if (c64 != null) {
            final boolean fire = attack && mc.screen == null;
            final String want = c64.has("want") ? c64.get("want").getAsString() : "";
            final int seq = c64.has("seq") ? c64.get("seq").getAsInt() : 0;
            final java.util.UUID id = player.getUUID();
            net.minecraft.client.server.IntegratedServer srv = mc.getSingleplayerServer();
            if (srv != null) srv.execute(() -> crb.c64.Craft64.INSTANCE.input(id, fire, want, seq));
            attack = false;
        }
        // Minecraft x Elden Combat: attack / guard / parry / dodge / lock-on counters go to the server-side combat rules;
        // the vanilla attack and use keys stay up while the mod is on (and are untouched when it is off).
        JsonObject ec = j.has("ec") && j.get("ec").isJsonObject() ? j.getAsJsonObject("ec") : null;
        boolean wasEc = ecDriving;
        ecDriving = ec != null;
        if (ec != null || wasEc) {
            final JsonObject in2 = ec == null ? null : ec.deepCopy();
            if (in2 != null) { in2.addProperty("fwd", fwd); in2.addProperty("strafe", strafe); in2.addProperty("yaw", camYaw); }
            final java.util.UUID id = player.getUUID();
            net.minecraft.client.server.IntegratedServer srv = mc.getSingleplayerServer();
            if (srv != null) srv.execute(() -> crb.ec.EldenCombat.INSTANCE.input(id, in2));
            if (ec != null) { attack = false; use = false; }
        }
        o.keyAttack.setDown(attack);
        o.keyUse.setDown(use);
        attackHeld = attack;
        // Monotonic press counters: edges are never lost when frames coalesce.
        if (!c64Driving && !ecDriving) lastAttackPresses = clicks(o.keyAttack, j, "attackPresses", lastAttackPresses);
        else if (j.has("attackPresses")) lastAttackPresses = j.get("attackPresses").getAsLong();
        if (!ecDriving) lastUsePresses = clicks(o.keyUse, j, "usePresses", lastUsePresses);
        else if (j.has("usePresses")) lastUsePresses = j.get("usePresses").getAsLong();
        lastSwapPresses = clicks(o.keySwapOffhand, j, "swapPresses", lastSwapPresses);
        lastDropPresses = clicks(o.keyDrop, j, "dropPresses", lastDropPresses);
        if (j.has("slot")) {
            int slot = j.get("slot").getAsInt();
            if (slot >= 0 && slot < 9) player.getInventory().selected = slot;
        }
        if (j.has("yaw") && j.has("pitch")) {
            float yaw = j.get("yaw").getAsFloat(), pitch = Mth.clamp(j.get("pitch").getAsFloat(), -90f, 90f);
            if (Float.isFinite(yaw) && Float.isFinite(pitch)) {
                if (!sm64Driving) { player.setYRot(yaw); player.setYHeadRot(yaw); } // SM64: Java owns the facing
                player.setXRot(pitch);
                appliedYaw = yaw; appliedPitch = pitch;
            }
        }
        holding = true;
        active = true;
        appliedSeq = in.seq();
        appliedCount++;
        lastAgeMs = ageMs;
        applyThread = Thread.currentThread().getName();
    }

    private long clicks(KeyMapping key, JsonObject j, String field, long last) {
        if (!j.has(field)) return last;
        long now = j.get(field).getAsLong();
        if (last >= 0 && now > last) {
            InputConstants.Key bound = KeyBindingHelper.getBoundKeyOf(key);
            for (long i = 0; i < Math.min(3, now - last); i++) KeyMapping.click(bound);
        }
        return now;
    }

    public void release(Options o) {
        for (KeyMapping k : new KeyMapping[] { o.keyUp, o.keyDown, o.keyLeft, o.keyRight, o.keyJump, o.keyShift, o.keySprint, o.keyAttack, o.keyUse })
            k.setDown(false);
        attackHeld = false;
        sm64Driving = false;
        holding = false;
        active = false;
    }

    private static float f(JsonObject j, String k) {
        if (!j.has(k)) return 0f;
        float v = j.get(k).getAsFloat();
        return Float.isFinite(v) ? Mth.clamp(v, -1f, 1f) : 0f;
    }

    private static boolean b(JsonObject j, String k) { return j.has(k) && j.get(k).getAsBoolean(); }
}
