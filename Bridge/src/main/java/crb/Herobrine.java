package crb;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.commands.CommandSourceStack;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.scores.Objective;
import net.minecraft.world.scores.Scoreboard;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * Control surface for the third-party "From The Fog" Herobrine mod (mod id "watching", Lunar Eclipse Studios,
 * CC BY-NC-SA 4.0), installed only in the dev client's Work\mc\mods. Nothing of the mod is copied: settings change
 * through the mod's OWN config functions (exactly what its in-chat config buttons run), sightings are started
 * through its OWN admin functions, and the current values are read from its scoreboard objective. Server thread.
 */
public final class Herobrine {
    public static final String MOD_ID = "watching";
    public static final String OBJECTIVE = "ftf.configOptions";

    /** Config option -> (scoreboard holder, allowed values). Values are the mod's own function names. */
    public record Option(String key, String holder, String label, List<String> values) {}
    public static final Map<String, Option> OPTIONS = new LinkedHashMap<>();
    private static void opt(String key, String holder, String label, String... values) { OPTIONS.put(key, new Option(key, holder, label, List.of(values))); }
    static {
        opt("stalking", "stalkingConfig", "Stalking (watches you from a distance)", "true", "false");
        opt("creeping", "creepingConfig", "Creeping (waits behind you)", "true", "false");
        opt("lurking", "lurkingConfig", "Lurking (whispers underground)", "true", "false");
        opt("nightmare", "nightmareMechanicConfig", "Nightmares", "true", "false");
        opt("jumpscare", "jumpscareConfig", "Jumpscares", "true", "false");
        opt("glowing_eyes", "glowingEyesConfig", "Glowing eyes", "true", "false");
        opt("ghost_door", "ghostDoorConfig", "Opens your doors", "true", "false");
        opt("ghost_mine", "ghostMineConfig", "Mines next to you", "true", "false");
        opt("disappearing_torches", "poofingTorchesConfig", "Breaks your torches", "true", "false");
        opt("burning_base", "burningBaseConfig", "Burns your base", "true", "false");
        opt("fearful_footsteps", "fearfulFootstepsConfig", "Footsteps", "true", "false");
        opt("no_sleep", "noSleepConfig", "Prevents sleeping", "true", "false");
        opt("sighting_chance", "sightingChanceConfig", "Sighting chance", "1_common", "2_uncommon", "3_rare");
        opt("start_delay", "dayDelayConfig", "Haunting start delay (days)", "off", "reset", "add", "remove");
    }
    public static final List<String> SIGHTINGS = List.of("stalking", "creeping", "lurking", "nightmare");
    private static final Pattern SAFE = Pattern.compile("[a-z0-9_]+");

    public static boolean installed() { return FabricLoader.getInstance().isModLoaded(MOD_ID); }

    private static int run(MinecraftServer server, ServerPlayer p, String command) {
        CommandSourceStack src = p.createCommandSourceStack().withPermission(4).withSuppressedOutput();
        return server.getCommands().performPrefixedCommand(src, command);
    }

    public static JsonObject status(MinecraftServer server, ServerPlayer p) {
        JsonObject r = ServerOps.ok(installed() ? "From The Fog loaded" : "From The Fog is not installed");
        r.addProperty("installed", installed());
        r.addProperty("version", FabricLoader.getInstance().getModContainer(MOD_ID).map(m -> m.getMetadata().getVersion().getFriendlyString()).orElse(""));
        Scoreboard sb = server.getScoreboard();
        Objective obj = sb.getObjective(OBJECTIVE);
        JsonArray a = new JsonArray();
        for (Option o : OPTIONS.values()) {
            JsonObject j = new JsonObject();
            j.addProperty("key", o.key()); j.addProperty("label", o.label());
            JsonArray v = new JsonArray(); o.values().forEach(v::add); j.add("values", v);
            j.addProperty("score", obj != null && sb.hasPlayerScore(o.holder(), obj) ? sb.getOrCreatePlayerScore(o.holder(), obj).getScore() : Integer.MIN_VALUE);
            a.add(j);
        }
        r.add("options", a);
        JsonArray s = new JsonArray(); SIGHTINGS.forEach(s::add); r.add("sightings", s);
        int rig = 0;
        for (Entity e : p.serverLevel().getAllEntities()) if (e.getTags().contains("herobrine")) rig++;
        r.addProperty("herobrineEntities", rig);
        return r;
    }

    public static JsonObject config(MinecraftServer server, ServerPlayer p, JsonObject args) {
        if (!installed()) return ServerOps.err("From The Fog is not installed");
        String key = args.has("key") ? args.get("key").getAsString() : "", value = args.has("value") ? args.get("value").getAsString() : "";
        Option o = OPTIONS.get(key);
        if (o == null || !o.values().contains(value) || !SAFE.matcher(key).matches() || !SAFE.matcher(value).matches()) return ServerOps.err("unknown setting " + key + "=" + value);
        int n = run(server, p, "function watching:config/" + key + "/" + value);
        JsonObject r = status(server, p);
        r.addProperty("ok", n > 0); r.addProperty("message", (n > 0 ? "Herobrine: " : "refused: ") + o.label() + " -> " + value);
        return r;
    }

    public static JsonObject summon(MinecraftServer server, ServerPlayer p, JsonObject args) {
        if (!installed()) return ServerOps.err("From The Fog is not installed");
        String kind = args.has("kind") ? args.get("kind").getAsString() : "";
        int n;
        if (kind.equals("fake")) {
            // The admin function builds the rig where it runs: place it 5 blocks in front of the player, facing them.
            n = run(server, p, "execute at @s rotated ~ 0 positioned ^ ^ ^5 run function fromthefog:admin/fake_herobrine/create");
            if (n > 0) run(server, p, "function fromthefog:admin/fake_herobrine/face_player");
        }
        else if (SIGHTINGS.contains(kind)) n = run(server, p, "function fromthefog:admin/sightings/" + kind);
        else return ServerOps.err("unknown sighting " + kind);
        JsonObject r = status(server, p);
        r.addProperty("ok", n > 0); r.addProperty("message", (n > 0 ? "Herobrine sighting started: " : "refused: ") + kind);
        return r;
    }

    public static JsonObject clear(MinecraftServer server, ServerPlayer p) {
        if (!installed()) return ServerOps.err("From The Fog is not installed");
        int n = run(server, p, "function fromthefog:admin/fake_herobrine/remove");
        run(server, p, "kill @e[tag=herobrine]");
        JsonObject r = status(server, p);
        r.addProperty("ok", true); r.addProperty("message", "Herobrine removed (" + n + ")");
        return r;
    }
}
