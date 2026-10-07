package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.GenericDirtMessageScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.nbt.NbtIo;
import net.minecraft.network.chat.Component;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.List;
import java.util.Locale;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Map selection (Unreal pause menu "Maps..."). Custom maps are Minecraft saves the user puts in the project's Maps
 * folder: a world folder (anything containing level.dat) or a zip such as a CurseForge modpack export, whose
 * overrides/saves/&lt;world&gt; is used. Loading a map copies it once into Work\mc\saves\crb-map-&lt;id&gt;, turns world
 * generation off for the overworld (flat generator with no layers and the void biome, so only the map's saved chunks
 * exist and everything beyond is empty) and opens it in the integrated server; Minecraft's own data fixers upgrade
 * older saves to 1.20.1. "superflat" recreates the default disposable superflat test world.
 * Render thread for list/load; copying and extracting run on a worker thread.
 */
public final class Maps {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    public static final String MAP_PREFIX = "crb-map-";
    public record Entry(String id, String name, Path source, boolean zip) {}

    public static volatile String status = "";
    public static volatile String loading = "";
    public static volatile String current = "superflat";
    public static volatile int progress; // 0..100 while importing

    static Path mapsDir(Minecraft mc) {
        // Work\mc -> project root \Maps
        return mc.gameDirectory.toPath().toAbsolutePath().normalize().getParent().getParent().resolve("Maps");
    }

    static String slug(String s) {
        String r = s.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "_").replaceAll("^_+|_+$", "");
        return r.isEmpty() ? "map" : r.substring(0, Math.min(40, r.length()));
    }

    public static List<Entry> list(Minecraft mc) {
        List<Entry> out = new ArrayList<>();
        Path dir = mapsDir(mc);
        if (!Files.isDirectory(dir)) return out;
        try (Stream<Path> s = Files.list(dir)) {
            for (Path p : s.sorted().toList()) {
                String n = p.getFileName().toString();
                if (Files.isDirectory(p) && findLevelDir(p, 5) != null) out.add(new Entry(slug(n), n, p, false));
                else if (n.toLowerCase(Locale.ROOT).endsWith(".zip") && zipWorldPrefix(p) != null) out.add(new Entry(slug(n.substring(0, n.length() - 4)), n.substring(0, n.length() - 4), p, true));
            }
        } catch (IOException e) { status = "cannot read Maps folder: " + e.getMessage(); }
        return out;
    }

    public static JsonObject listJson(Minecraft mc) {
        JsonObject r = new JsonObject();
        JsonArray a = new JsonArray();
        JsonObject flat = new JsonObject(); flat.addProperty("id", "superflat"); flat.addProperty("name", "Superflat (default test world)"); flat.addProperty("source", "generated"); a.add(flat);
        for (Entry e : list(mc)) {
            JsonObject o = new JsonObject(); o.addProperty("id", e.id()); o.addProperty("name", e.name()); o.addProperty("source", e.zip() ? "zip" : "folder");
            o.addProperty("imported", Files.isDirectory(mc.getLevelSource().getBaseDir().resolve(MAP_PREFIX + e.id())));
            a.add(o);
        }
        r.add("maps", a);
        r.addProperty("folder", mapsDir(mc).toString());
        r.addProperty("current", current);
        r.addProperty("message", (a.size() - 1) + " custom maps");
        return r;
    }

    /** Starts loading a map. Returns a message; the switch itself happens asynchronously. */
    public static String load(Minecraft mc, String id) {
        if (!loading.isEmpty()) return "already loading " + loading;
        if (id.equals("superflat")) {
            loading = id;
            leaveWorld(mc);
            current = "superflat";
            DevWorld.createNow(mc);
            loading = "";
            return "loading superflat";
        }
        Entry e = list(mc).stream().filter(x -> x.id().equals(id)).findFirst().orElse(null);
        if (e == null) return "unknown map " + id;
        Path saves = mc.getLevelSource().getBaseDir();
        Path target = saves.resolve(MAP_PREFIX + e.id());
        loading = id; progress = 0; status = "importing " + e.name();
        Thread t = new Thread(() -> {
            try {
                if (!Files.isDirectory(target)) {
                    Path tmp = saves.resolve(MAP_PREFIX + e.id() + ".tmp");
                    deleteTree(tmp);
                    if (e.zip()) extract(e.source(), tmp); else copyTree(findLevelDir(e.source(), 5), tmp);
                    Files.move(tmp, target, StandardCopyOption.ATOMIC_MOVE);
                }
                disableWorldGen(target.resolve("level.dat").toFile());
                status = "opening " + e.name();
                mc.execute(() -> {
                    leaveWorld(mc);
                    current = e.id();
                    LOG.info("Opening map {} ({})", e.name(), target.getFileName());
                    mc.createWorldOpenFlows().doLoadLevel(new TitleScreen(), target.getFileName().toString(), false, false);
                    loading = "";
                });
            } catch (Exception ex) {
                LOG.error("Map import failed", ex);
                status = "map import failed: " + ex.getMessage();
                loading = "";
            }
        }, "crb-map-import");
        t.setDaemon(true);
        t.start();
        return "importing " + e.name();
    }

    static void leaveWorld(Minecraft mc) {
        if (mc.level == null) return;
        boolean local = mc.isLocalServer();
        mc.level.disconnect();
        if (local) mc.clearLevel(new GenericDirtMessageScreen(Component.translatable("menu.savingLevel")));
        else mc.clearLevel();
    }

    /** Overworld generator -> flat, no layers, void biome: nothing is generated outside the map's saved chunks. */
    static void disableWorldGen(File levelDat) throws IOException {
        CompoundTag root = NbtIo.readCompressed(levelDat);
        CompoundTag data = root.getCompound("Data");
        int version = data.getInt("DataVersion");
        if (!data.contains("WorldGenSettings") && version < 2566) {
            // Pre-1.16 save (e.g. 1.12 Forge maps): the old generator fields; Minecraft's data fixers turn them into
            // WorldGenSettings on load. Flat with a single air layer over the void biome = no terrain at all.
            data.putString("generatorName", "flat");
            data.putInt("generatorVersion", 0);
            if (version < 1466) data.putString("generatorOptions", "3;minecraft:air;127;");
            else {
                CompoundTag o = new CompoundTag(); ListTag layers = new ListTag(); CompoundTag air = new CompoundTag();
                air.putString("block", "minecraft:air"); air.putInt("height", 1); layers.add(air);
                o.put("layers", layers); o.putString("biome", "minecraft:the_void"); o.put("structures", new CompoundTag());
                data.put("generatorOptions", o);
            }
            data.putBoolean("MapFeatures", false);
            root.put("Data", data);
            NbtIo.writeCompressed(root, levelDat);
            return;
        }
        CompoundTag wgs = data.getCompound("WorldGenSettings");
        CompoundTag dims = wgs.getCompound("dimensions");
        CompoundTag over = dims.getCompound("minecraft:overworld");
        CompoundTag gen = new CompoundTag();
        gen.putString("type", "minecraft:flat");
        CompoundTag settings = new CompoundTag();
        settings.putString("biome", "minecraft:the_void");
        settings.put("layers", new ListTag());
        settings.putBoolean("lakes", false);
        settings.putBoolean("features", false);
        if (version < 2900) { CompoundTag st = new CompoundTag(); st.put("structures", new CompoundTag()); settings.put("structures", st); } // 1.16-1.17 format, upgraded by the data fixers
        else settings.put("structure_overrides", new ListTag());
        gen.put("settings", settings);
        over.put("generator", gen);
        if (!over.contains("type")) over.putString("type", "minecraft:overworld");
        dims.put("minecraft:overworld", over);
        wgs.put("dimensions", dims);
        data.put("WorldGenSettings", wgs);
        data.putString("LevelName", data.getString("LevelName").isEmpty() ? levelDat.getParentFile().getName() : data.getString("LevelName"));
        root.put("Data", data);
        NbtIo.writeCompressed(root, levelDat);
    }

    static Path findLevelDir(Path p, int depth) throws IOException {
        if (Files.isRegularFile(p.resolve("level.dat"))) return p;
        if (depth <= 0) return null;
        try (Stream<Path> s = Files.list(p)) {
            for (Path c : s.filter(Files::isDirectory).sorted().toList()) { Path r = findLevelDir(c, depth - 1); if (r != null) return r; }
        }
        return null;
    }

    /** Zip path prefix (ending in '/') of the world folder holding level.dat (shallowest), or null. */
    static String zipWorldPrefix(Path zip) {
        try (ZipFile z = new ZipFile(zip.toFile())) {
            String best = null;
            Enumeration<? extends ZipEntry> en = z.entries();
            while (en.hasMoreElements()) {
                String n = en.nextElement().getName().replace('\\', '/');
                if (!n.endsWith("level.dat") || !(n.equals("level.dat") || n.endsWith("/level.dat"))) continue;
                String prefix = n.substring(0, n.length() - "level.dat".length());
                if (best == null || prefix.chars().filter(c -> c == '/').count() < best.chars().filter(c -> c == '/').count()) best = prefix;
            }
            return best;
        } catch (IOException e) { return null; }
    }

    static void extract(Path zip, Path dest) throws IOException {
        String prefix = zipWorldPrefix(zip);
        if (prefix == null) throw new IOException("no level.dat in " + zip.getFileName());
        Files.createDirectories(dest);
        Path root = dest.toAbsolutePath().normalize();
        try (ZipFile z = new ZipFile(zip.toFile())) {
            List<? extends ZipEntry> all = z.stream().filter(x -> x.getName().replace('\\', '/').startsWith(prefix)).toList();
            int i = 0;
            for (ZipEntry en : all) {
                String rel = en.getName().replace('\\', '/').substring(prefix.length());
                progress = (int) (100L * ++i / Math.max(1, all.size()));
                if (rel.isEmpty()) continue;
                Path out = root.resolve(rel).normalize();
                if (!out.startsWith(root)) throw new IOException("unsafe zip entry " + en.getName()); // zip-slip guard
                if (en.isDirectory()) { Files.createDirectories(out); continue; }
                Files.createDirectories(out.getParent());
                try (InputStream in = z.getInputStream(en)) { Files.copy(in, out, StandardCopyOption.REPLACE_EXISTING); }
            }
        }
    }

    static void copyTree(Path src, Path dest) throws IOException {
        if (src == null) throw new IOException("no level.dat in map folder");
        try (Stream<Path> walk = Files.walk(src)) {
            List<Path> all = walk.toList(); int i = 0;
            for (Path p : all) {
                Path out = dest.resolve(src.relativize(p).toString());
                progress = (int) (100L * ++i / Math.max(1, all.size()));
                if (Files.isDirectory(p)) Files.createDirectories(out); else { Files.createDirectories(out.getParent()); Files.copy(p, out, StandardCopyOption.REPLACE_EXISTING); }
            }
        }
    }

    static void deleteTree(Path root) {
        if (!Files.exists(root)) return;
        try (Stream<Path> walk = Files.walk(root)) {
            walk.sorted(Comparator.reverseOrder()).forEach(p -> { try { Files.delete(p); } catch (IOException ignored) { } });
        } catch (IOException ignored) { }
    }
}
