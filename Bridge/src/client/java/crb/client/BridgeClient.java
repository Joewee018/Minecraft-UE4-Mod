package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import crb.BridgeMod;
import crb.Endpoint;
import crb.ServerOps;
import crb.Wire;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.fabricmc.fabric.api.resource.ResourceManagerHelper;
import net.fabricmc.fabric.api.resource.SimpleSynchronousResourceReloadListener;
import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.ModContainer;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.AccessibilityOnboardingScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.client.server.IntegratedServer;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.packs.PackType;
import net.minecraft.server.packs.resources.ResourceManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.file.Path;
import java.util.UUID;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Client entry point. Thread ownership:
 *  - socket threads (Endpoint) only move bytes and enqueue commands;
 *  - the Minecraft client/render thread applies input and runs every exporter;
 *  - world mutations run on the integrated server thread via server.execute (ServerOps).
 */
public final class BridgeClient implements ClientModInitializer, Endpoint.Listener {
    /** Craft 64 HUD art (weapon sprites, muzzle flashes, status face) shipped in assets/crossover_rebuilt/textures/c64. */
    static final String[] C64_TEXTURES = crb.c64.C64Art.NAMES;

    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    private static final int MAX_COMMANDS = 64;
    private Endpoint endpoint;
    private Path endpointFile;
    private final ConcurrentLinkedQueue<JsonObject> commands = new ConcurrentLinkedQueue<>();
    private final AtomicInteger queued = new AtomicInteger();
    private final HostInput input = new HostInput();
    private final TextureExporter textures = new TextureExporter();
    private final WorldExporter world = new WorldExporter();
    private final PoseExporter poses = new PoseExporter();
    private final StateExporter state = new StateExporter();
    private final ParticleExporter particles = new ParticleExporter();
    private final DebugCapture debugCapture = new DebugCapture();
    private final TitleExporter titles = new TitleExporter();
    private volatile int connectionEpoch;
    private volatile boolean resetPending;
    private int seenConnection;
    private long commandsHandled, commandsRejected;
    private volatile String serverOpsThread = "";

    @Override
    public void onInitializeClient() {
        // The thrown axe has no vanilla model; Unreal draws it from AxeEntity.VIEW. Registered unconditionally so a
        // tracked axe never lacks a renderer.
        net.fabricmc.fabric.api.client.rendering.v1.EntityRendererRegistry.register(BridgeMod.AXE, net.minecraft.client.renderer.entity.NoopRenderer::new);
        net.fabricmc.fabric.api.client.rendering.v1.EntityRendererRegistry.register(BridgeMod.MUTANT, net.minecraft.client.renderer.entity.NoopRenderer::new);
        // Zombies mode: vanilla zombie model as the stand-in until Unreal draws the Mixamo zombies.
        net.fabricmc.fabric.api.client.rendering.v1.EntityRendererRegistry.register(BridgeMod.ZM_ZOMBIE, crb.client.zm.ZmZombieRenderer::new);
        String endpointProp = System.getProperty("crb.endpoint");
        if (endpointProp == null || endpointProp.isBlank()) {
            LOG.info("crb.endpoint not set; Crossover-Rebuilt bridge disabled");
            return;
        }
        endpointFile = Path.of(endpointProp);
        endpoint = new Endpoint(this, welcome().toString());
        try {
            endpoint.start(endpointFile, BridgeMod.VERSION);
        } catch (Exception ex) {
            LOG.error("Could not start the Crossover-Rebuilt bridge endpoint", ex);
            endpoint = null;
            return;
        }
        BridgeMod.ops().setEventSink(e -> { if (endpoint != null) endpoint.control(Wire.EVENT, e.toString(), null); });
        ClientTickEvents.START_CLIENT_TICK.register(this::startTick);
        ClientTickEvents.END_CLIENT_TICK.register(this::endTick);
        ClientLifecycleEvents.CLIENT_STOPPING.register(mc -> endpoint.stop(endpointFile));
        ScreenEvents.AFTER_INIT.register((mc, screen, w, h) -> {
            if (screen instanceof TitleScreen || screen instanceof AccessibilityOnboardingScreen) DevWorld.maybeCreate(mc);
        });
        ClientPlayConnectionEvents.DISCONNECT.register((h, mc) -> { resetPending = true; crb.client.sm64.Sm64Controller.INSTANCE.reset(null); crb.pp.Portals.ENABLED = false; });
        ClientPlayConnectionEvents.JOIN.register((h, s, mc) -> resetPending = true);
        ResourceManagerHelper.get(PackType.CLIENT_RESOURCES).registerReloadListener(new SimpleSynchronousResourceReloadListener() {
            @Override public ResourceLocation getFabricId() { return new ResourceLocation("crossover_rebuilt", "reload"); }
            @Override public void onResourceManagerReload(ResourceManager manager) { resetPending = true; }
        });
    }

    private static JsonObject welcome() {
        JsonObject w = new JsonObject();
        w.addProperty("protocol", Wire.PROTOCOL);
        w.addProperty("bridge", BridgeMod.VERSION);
        w.addProperty("minecraft", SharedConstants.getCurrentVersion().getName());
        w.addProperty("loader", version("fabricloader"));
        w.addProperty("fabricApi", version("fabric-api"));
        w.addProperty("mappings", "Mojang official (Loom officialMojangMappings)");
        w.addProperty("java", System.getProperty("java.version"));
        JsonArray mods = new JsonArray();
        for (ModContainer m : FabricLoader.getInstance().getAllMods()) {
            String id = m.getMetadata().getId();
            if (id.startsWith("fabric-") && !id.equals("fabric-api")) continue; // API modules are listed as fabric-api
            JsonObject o = new JsonObject();
            o.addProperty("id", id); o.addProperty("name", m.getMetadata().getName()); o.addProperty("version", m.getMetadata().getVersion().getFriendlyString());
            mods.add(o);
        }
        w.add("fabricMods", mods);
        return w;
    }

    private static String version(String id) {
        return FabricLoader.getInstance().getModContainer(id).map(m -> m.getMetadata().getVersion().getFriendlyString()).orElse("?");
    }

    // ---------------- Endpoint.Listener (socket threads) ----------------
    @Override public void onConnected(int id) { connectionEpoch = id; resetPending = true; }

    @Override public void onDisconnected(int id, String reason) {
        resetPending = true;
        Minecraft mc = Minecraft.getInstance();
        mc.execute(() -> {
            input.release(mc.options);
            IntegratedServer server = mc.getSingleplayerServer();
            UUID pid = ServerOps.controlledPlayer;
            if (server != null) server.execute(() -> BridgeMod.ops().cleanup(server, pid));
        });
    }

    @Override public void onCommand(int id, JsonObject command) {
        if (queued.incrementAndGet() > MAX_COMMANDS) {
            queued.decrementAndGet();
            commandsRejected++;
            JsonObject r = ServerOpsResult.error(command, "command queue full");
            endpoint.control(Wire.RESULT, r.toString(), null);
            return;
        }
        commands.add(command);
    }

    // ---------------- client thread ----------------
    private void startTick(Minecraft mc) {
        if (resetPending || seenConnection != connectionEpoch) {
            resetPending = false; seenConnection = connectionEpoch;
            textures.reset(); world.reset(); state.reset();
        }
        if (mc.player != null) ServerOps.controlledPlayer = mc.player.getUUID();
        input.apply(mc, endpoint);
        JsonObject cmd;
        int n = 0;
        while (n++ < 8 && (cmd = commands.poll()) != null) {
            queued.decrementAndGet();
            handle(mc, cmd);
        }
    }

    private void handle(Minecraft mc, JsonObject cmd) {
        String op = cmd.has("op") ? cmd.get("op").getAsString() : "";
        JsonObject args = cmd.has("args") && cmd.get("args").isJsonObject() ? cmd.getAsJsonObject("args") : new JsonObject();
        commandsHandled++;
        if (op.equals("ping")) { endpoint.control(Wire.RESULT, ServerOpsResult.ok(cmd, "pong").toString(), null); return; }
        if (op.equals("mods")) { endpoint.control(Wire.MODS, welcome().toString(), null); return; }
        if (op.equals("resend")) { world.invalidateModels(); textures.reset(); return; }
        if (op.equals("inv.click") || op.equals("inv.close")) { inventory(mc, cmd, op, args); return; }
        if (op.startsWith("creative.")) { creative(mc, cmd, op, args); return; }
        if (op.equals("player.respawn")) {
            // Vanilla DeathScreen "Respawn": LocalPlayer.respawn() (PERFORM_RESPAWN) and close the screen.
            if (mc.player == null) { endpoint.control(Wire.RESULT, ServerOpsResult.error(cmd, "no player").toString(), null); return; }
            boolean dead = mc.player.isDeadOrDying() || mc.screen instanceof net.minecraft.client.gui.screens.DeathScreen;
            if (dead) { mc.player.respawn(); mc.setScreen(null); }
            endpoint.control(Wire.RESULT, (dead ? ServerOpsResult.ok(cmd, "respawn requested") : ServerOpsResult.error(cmd, "player is alive")).toString(), null);
            return;
        }
        if (op.equals("game.quit")) {
            // Pause menu "Quit Game": Minecraft.stop() ends the run loop; shutdown disconnects, saves the
            // integrated world and closes the client (the bridge endpoint file is removed on CLIENT_STOPPING).
            endpoint.control(Wire.RESULT, ServerOpsResult.ok(cmd, "saving and quitting Minecraft").toString(), null);
            LOG.info("Unreal host requested quit: saving the world and stopping the client");
            mc.stop();
            return;
        }
        if (op.equals("pp.status") || op.equals("pp.debug") || op.equals("pp.resetStats")) {
            // Physics & Portal mod (client thread): status, debug menu commands (physics side), stats.
            crb.client.pp.PPController pc = crb.client.pp.PPController.INSTANCE;
            JsonObject r;
            switch (op) {
                case "pp.status" -> { r = pc.export(mc.player); r.add("config", crb.client.pp.PPConfig.toJson()); r.addProperty("message", "pp " + (pc.active() ? "active" : "inactive")); }
                case "pp.resetStats" -> { pc.resetStats(); r = new JsonObject(); r.addProperty("message", "pp stats cleared"); }
                default -> {
                    String c = args.has("cmd") ? args.get("cmd").getAsString() : "";
                    String m;
                    try { m = pc.debug(c, args); } catch (RuntimeException ex) { m = "failed: " + ex; }
                    r = new JsonObject();
                    if (m == null) { r.addProperty("ok", false); r.addProperty("message", "unknown pp.debug cmd '" + c + "'"); }
                    else r.addProperty("message", m);
                    r.add("config", crb.client.pp.PPConfig.toJson());
                }
            }
            if (!r.has("ok")) r.addProperty("ok", true);
            r.addProperty("thread", Thread.currentThread().getName());
            endpoint.control(Wire.RESULT, ServerOpsResult.wrap(cmd, r).toString(), null);
            return;
        }
        if (op.startsWith("sm64.")) {
            // SM64 Steve Movement (client thread): settings from the Unreal Mod Menu, status/evidence for tests.
            JsonObject r;
            crb.client.sm64.Sm64Controller sm = crb.client.sm64.Sm64Controller.INSTANCE;
            switch (op) {
                case "sm64.config" -> { crb.client.sm64.Sm64Config.apply(args); sm.configChanged(); r = crb.client.sm64.Sm64Config.toJson(); r.addProperty("message", "sm64 settings applied"); }
                case "sm64.status" -> { r = sm.export(mc.player); r.add("config", crb.client.sm64.Sm64Config.toJson()); r.addProperty("message", "sm64 " + (sm.active() ? "active" : "inactive")); }
                case "sm64.resetStats" -> { sm.resetStats(); r = new JsonObject(); r.addProperty("message", "sm64 stats cleared"); }
                default -> { r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", "unknown op " + op); }
            }
            if (!r.has("ok")) r.addProperty("ok", true);
            r.addProperty("thread", Thread.currentThread().getName());
            endpoint.control(Wire.RESULT, ServerOpsResult.wrap(cmd, r).toString(), null);
            return;
        }
        if (op.equals("game.pause")) {
            // "Pause game": Minecraft's own singleplayer pause (the integrated server stops ticking).
            boolean on = args.has("on") && args.get("on").getAsBoolean();
            if (on) mc.pauseGame(false); else if (mc.screen instanceof net.minecraft.client.gui.screens.PauseScreen) mc.setScreen(null);
            endpoint.control(Wire.RESULT, ServerOpsResult.ok(cmd, on ? "game paused" : "game resumed").toString(), null);
            return;
        }
        if (op.equals("map.list") || op.equals("map.load")) {
            JsonObject r;
            try {
                if (op.equals("map.list")) r = Maps.listJson(mc);
                else { r = new JsonObject(); String msg = Maps.load(mc, args.has("id") ? args.get("id").getAsString() : ""); r.addProperty("message", msg); r.addProperty("ok", !msg.startsWith("unknown") && !msg.startsWith("already")); }
                if (!r.has("ok")) r.addProperty("ok", true);
            } catch (Exception ex) { r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", ex.toString()); }
            r.addProperty("thread", Thread.currentThread().getName());
            endpoint.control(Wire.RESULT, ServerOpsResult.wrap(cmd, r).toString(), null);
            return;
        }
        if (op.equals("debug.capture")) {
            debugCapture.request(mc, args.has("name") ? args.get("name").getAsString() : "capture", args.has("camera") ? args.get("camera").getAsString() : "first");
            endpoint.control(Wire.RESULT, ServerOpsResult.ok(cmd, "capture scheduled").toString(), null);
            return;
        }
        IntegratedServer server = mc.getSingleplayerServer();
        if (server == null || mc.player == null) { endpoint.control(Wire.RESULT, ServerOpsResult.error(cmd, "no integrated server / player").toString(), null); return; }
        UUID pid = mc.player.getUUID();
        server.execute(() -> {
            JsonObject r;
            try {
                serverOpsThread = Thread.currentThread().getName();
                r = BridgeMod.ops().handle(server, pid, op, args);
            } catch (Exception ex) {
                r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", ex.toString());
            }
            r.addProperty("thread", Thread.currentThread().getName());
            endpoint.control(Wire.RESULT, ServerOpsResult.wrap(cmd, r).toString(), null);
        });
    }

    /** Player-inventory clicks through vanilla's own path (render thread -> MultiPlayerGameMode -> server menu). */
    private void inventory(Minecraft mc, JsonObject cmd, String op, JsonObject args) {
        if (mc.player == null || mc.gameMode == null) { endpoint.control(Wire.RESULT, ServerOpsResult.error(cmd, "no player").toString(), null); return; }
        if (op.equals("inv.close")) {
            // Vanilla close: server returns the 2x2 grid and the cursor stack to the inventory (or drops them).
            mc.player.closeContainer();
            endpoint.control(Wire.RESULT, ServerOpsResult.ok(cmd, "inventory closed").toString(), null);
            return;
        }
        int slot = args.has("slot") ? args.get("slot").getAsInt() : -1;
        int button = args.has("button") ? args.get("button").getAsInt() : 0;
        boolean shift = args.has("shift") && args.get("shift").getAsBoolean();
        if (slot < 0 || slot > 45 || button < 0 || button > 1 || mc.player.containerMenu != mc.player.inventoryMenu) {
            endpoint.control(Wire.RESULT, ServerOpsResult.error(cmd, "invalid inventory click").toString(), null);
            return;
        }
        mc.gameMode.handleInventoryMouseClick(mc.player.inventoryMenu.containerId, slot, button,
            shift ? net.minecraft.world.inventory.ClickType.QUICK_MOVE : net.minecraft.world.inventory.ClickType.PICKUP, mc.player);
        JsonObject r = ServerOpsResult.ok(cmd, "clicked slot " + slot);
        r.addProperty("thread", Thread.currentThread().getName());
        endpoint.control(Wire.RESULT, r.toString(), null);
    }

    /** Creative inventory ops (render thread, vanilla creative paths; see CreativeOps). */
    private void creative(Minecraft mc, JsonObject cmd, String op, JsonObject args) {
        JsonObject r;
        try {
            if (mc.player == null || mc.gameMode == null) throw new IllegalStateException("no player");
            boolean creative = mc.gameMode.getPlayerMode().isCreative();
            int tab = args.has("tab") ? args.get("tab").getAsInt() : 0, row = args.has("row") ? args.get("row").getAsInt() : 0;
            String query = args.has("query") ? args.get("query").getAsString() : "";
            if (query.length() > 50) query = query.substring(0, 50);
            switch (op) {
                case "creative.tabs" -> r = CreativeOps.tabsResult(mc, textures);
                case "creative.page" -> r = CreativeOps.page(mc, textures, tab, row, query);
                case "creative.pick" -> { if (!creative) throw new IllegalStateException("not in creative mode"); r = new JsonObject(); r.addProperty("message", CreativeOps.pick(mc, tab, row, query, args.get("cell").getAsInt(), args.has("button") ? args.get("button").getAsInt() : 0)); }
                case "creative.slot" -> { if (!creative) throw new IllegalStateException("not in creative mode"); r = new JsonObject(); r.addProperty("message", CreativeOps.slot(mc, args.get("slot").getAsInt(), args.has("button") ? args.get("button").getAsInt() : 0)); }
                case "creative.destroy" -> { if (!creative) throw new IllegalStateException("not in creative mode"); r = new JsonObject(); r.addProperty("message", CreativeOps.destroy(mc, args.has("all") && args.get("all").getAsBoolean())); }
                default -> throw new IllegalArgumentException("unknown op " + op);
            }
            r.addProperty("ok", true);
            if (!r.has("message")) r.addProperty("message", op);
        } catch (Exception ex) {
            r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", ex.getMessage() == null ? ex.toString() : ex.getMessage());
        }
        r.addProperty("thread", Thread.currentThread().getName());
        endpoint.control(Wire.RESULT, ServerOpsResult.wrap(cmd, r).toString(), null);
    }

    private void endTick(Minecraft mc) {
        debugCapture.tick(mc);
        if (endpoint == null || !endpoint.connected() || mc.player == null || mc.level == null) return;
        int epoch = connectionEpoch;
        world.tick(mc, endpoint);
        poses.tick(mc, endpoint, textures, epoch);
        particles.tick(mc, endpoint, textures);
        textures.request(new ResourceLocation(ParticleExporter.BLOCK_ATLAS));
        JsonObject extra = new JsonObject();
        extra.addProperty("epoch", epoch);
        extra.addProperty("poseSeq", poses.seq());
        extra.addProperty("poseWalkPos", poses.lastWalkPos);
        extra.addProperty("poseErrors", poses.errors);
        extra.addProperty("poseVertices", poses.lastVertices);
        extra.addProperty("inputSeq", input.appliedSeq);
        extra.addProperty("inputApplied", input.appliedCount);
        extra.addProperty("inputActive", HostInput.active());
        extra.addProperty("inputAgeMs", input.lastAgeMs);
        extra.addProperty("inputThread", input.applyThread);
        extra.addProperty("serverOpsThread", serverOpsThread);
        extra.addProperty("texturesSent", textures.sent);
        extra.addProperty("textureFailures", textures.failed);
        extra.addProperty("textureLastError", textures.lastError);
        extra.addProperty("texturesPending", textures.pending());
        extra.addProperty("sectionsSent", world.sections);
        extra.addProperty("modelsSent", world.models);
        extra.addProperty("particles", particles.exported);
        extra.addProperty("framesOut", endpoint.framesOut.get());
        extra.addProperty("droppedOut", endpoint.droppedOut.get());
        extra.addProperty("oversizeOut", endpoint.oversizeOut.get());
        extra.addProperty("commandsHandled", commandsHandled);
        extra.addProperty("bridge", BridgeMod.VERSION);
        extra.addProperty("entityErrors", poses.entityErrors);
        extra.add("sm64", crb.client.sm64.Sm64Controller.INSTANCE.export(mc.player));
        crb.client.pp.PPController.INSTANCE.clientTick(mc);
        extra.add("pp", crb.client.pp.PPController.INSTANCE.export(mc.player));
        extra.add("ec", crb.client.ec.ECClient.export(mc));   // Minecraft x Elden Combat
        JsonObject c64 = crb.c64.Craft64.viewJson();
        extra.add("c64", c64);
        if (c64.get("on").getAsBoolean()) {
            for (String t : C64_TEXTURES) textures.request(new net.minecraft.resources.ResourceLocation("crossover_rebuilt", "textures/c64/" + t + ".png"));
            for (String t : crb.c64.C64Art.VANILLA) textures.request(new net.minecraft.resources.ResourceLocation("minecraft", t));
        }
        if (crb.client.sm64.Sm64Controller.INSTANCE.requested()) textures.request(mc.player.getSkinTextureLocation()); // Steve's runtime skin
        try { titles.tick(mc, textures, extra); } catch (Exception ex) { titles.errors++; titles.lastError = ex.toString(); }
        state.tick(mc, endpoint, textures, extra);
        textures.tick(mc, endpoint);
    }
}
