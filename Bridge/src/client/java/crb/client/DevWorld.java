package crb.client;

import crb.ServerOps;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.Registries;
import net.minecraft.world.Difficulty;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.text.SimpleDateFormat;
import java.util.Comparator;
import java.util.Date;
import java.util.Locale;
import java.util.stream.Stream;

/**
 * Development-only (-Dcrb.devWorld=true): creates a fresh, disposable world inside this project's Work\mc\saves
 * directory. The type comes from Work\mc\crb-world-type.txt: "superflat" (default; the test world, fixtures and
 * server ops enabled) or "debug" (vanilla Debug Mode: every block state on a grid, spectator, no server ops).
 * Previous crb-superflat / crb-debug worlds in that directory are deleted. Never touches other saves.
 */
public final class DevWorld {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    private static boolean requested;
    public static final String DEBUG_WORLD_PREFIX = "crb-debug";

    public static void maybeCreate(Minecraft mc) {
        if (requested || !Boolean.getBoolean("crb.devWorld") || mc.level != null) return;
        requested = true;
        mc.tell(() -> create(mc));
    }

    /** Map menu "Superflat": a fresh disposable superflat world now (the caller already left the current world). */
    public static void createNow(Minecraft mc) { requested = true; create(mc); }

    private static void create(Minecraft mc) {
        Path saves = mc.getLevelSource().getBaseDir();
        // Safety: only the project's own development run directory is cleaned.
        if (!saves.toAbsolutePath().normalize().toString().replace('\\', '/').contains("/Work/mc/")) {
            LOG.warn("Refusing to manage disposable worlds outside Work/mc: {}", saves);
            return;
        }
        String type = "superflat";
        try { Path f = mc.gameDirectory.toPath().resolve("crb-world-type.txt"); if (Files.exists(f)) type = Files.readString(f).trim().toLowerCase(Locale.ROOT); } catch (IOException ignored) { }
        final boolean debug = type.equals("debug");
        try (Stream<Path> s = Files.list(saves)) {
            s.filter(p -> { String n = p.getFileName().toString(); return n.startsWith(ServerOps.TEST_WORLD_PREFIX) || n.startsWith(DEBUG_WORLD_PREFIX); }).forEach(DevWorld::deleteTree);
        } catch (IOException ignored) { }
        String name = (debug ? DEBUG_WORLD_PREFIX : ServerOps.TEST_WORLD_PREFIX) + "-" + new SimpleDateFormat("yyyyMMdd-HHmmss").format(new Date());
        GameRules rules = new GameRules();
        rules.getRule(GameRules.RULE_DOMOBSPAWNING).set(false, null);
        rules.getRule(GameRules.RULE_DAYLIGHT).set(false, null);
        rules.getRule(GameRules.RULE_WEATHER_CYCLE).set(false, null);
        // Vanilla Debug Mode puts the player in spectator; the superflat test world is survival.
        LevelSettings settings = new LevelSettings(name, debug ? GameType.SPECTATOR : GameType.SURVIVAL, false, Difficulty.EASY, true, rules, WorldDataConfiguration.DEFAULT);
        WorldOptions options = new WorldOptions(20261005L, false, false);
        LOG.info("Creating disposable {} world {}", debug ? "debug-mode" : "superflat test", name);
        mc.createWorldOpenFlows().createFreshLevel(name, settings, options,
            access -> access.registryOrThrow(Registries.WORLD_PRESET).getHolderOrThrow(debug ? WorldPresets.DEBUG : WorldPresets.FLAT).value().createWorldDimensions());
    }

    private static void deleteTree(Path root) {
        try (Stream<Path> walk = Files.walk(root)) {
            walk.sorted(Comparator.reverseOrder()).forEach(p -> { try { Files.delete(p); } catch (IOException ignored) { } });
        } catch (IOException ignored) { }
    }
}
