package crb.client.pp;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.tags.TagKey;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * Physics & Portal mod - data-driven Minecraft surface physics (assets/crossover_rebuilt/pp/surfaces.json). Block ids
 * win over block tags; anything else uses "default". Results are cached per Block (no per-step map lookups by string).
 */
public final class PPSurfaces {
    public record Surface(String name, double friction, double accel, double maxSpeed, double bounce, double drag, double roll) {}
    public static final Surface DEFAULT = new Surface("default", 6, 1, 1, 0, 0, 1);
    private static final Map<String, Surface> BY_ID = new HashMap<>();
    private static final List<Map.Entry<TagKey<Block>, Surface>> BY_TAG = new ArrayList<>();
    private static final Map<Block, Surface> CACHE = new IdentityHashMap<>();
    private static Surface def = DEFAULT;
    public static String loadError = "";

    static {
        try (InputStream in = PPSurfaces.class.getResourceAsStream("/assets/crossover_rebuilt/pp/surfaces.json")) {
            if (in == null) throw new IllegalStateException("surfaces.json missing");
            JsonObject root = JsonParser.parseReader(new InputStreamReader(in, StandardCharsets.UTF_8)).getAsJsonObject();
            for (Map.Entry<String, JsonElement> e : root.entrySet()) {
                if (e.getKey().startsWith("_") || !e.getValue().isJsonObject()) continue;
                JsonObject o = e.getValue().getAsJsonObject();
                Surface s = new Surface(e.getKey(), d(o, "friction", 6), d(o, "accel", 1), d(o, "maxSpeed", 1), d(o, "bounce", 0), d(o, "drag", 0), d(o, "roll", 1));
                if (e.getKey().equals("default")) def = s;
                else if (e.getKey().startsWith("#")) BY_TAG.add(Map.entry(TagKey.create(Registries.BLOCK, new ResourceLocation(e.getKey().substring(1))), s));
                else BY_ID.put(e.getKey(), s);
            }
        } catch (Exception ex) { loadError = ex.toString(); }
    }

    private static double d(JsonObject o, String k, double def) { return o.has(k) ? o.get(k).getAsDouble() : def; }

    public static int count() { return BY_ID.size() + BY_TAG.size() + 1; }

    public static Surface of(BlockState state) {
        Block b = state.getBlock();
        Surface s = CACHE.get(b);
        if (s != null) return s;
        s = BY_ID.get(BuiltInRegistries.BLOCK.getKey(b).toString());
        if (s == null) for (Map.Entry<TagKey<Block>, Surface> e : BY_TAG) if (state.is(e.getKey())) { s = e.getValue(); break; }
        if (s == null) s = def;
        CACHE.put(b, s);
        return s;
    }
}
