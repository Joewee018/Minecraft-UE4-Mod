package crb.client.ec;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.NativeImage;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.world.item.ItemStack;

import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

/**
 * Minecraft x Elden Combat, client thread: the "ec" export block. Adds the held weapon's own item sprite as pixels
 * (RRGGBBAA hex, sent every 10 ticks while the mod is on) so Unreal can build the 3D extruded weapon Steve holds.
 */
public final class ECClient {
    private ECClient() { }
    private static final Map<String, String> PIXELS = new HashMap<>();
    private static long n;

    public static JsonObject export(Minecraft mc) {
        JsonObject j = crb.ec.EldenCombat.viewJson();
        if (!j.get("on").getAsBoolean() || mc.player == null) return j;
        ItemStack st = mc.player.getMainHandItem();
        if (!st.isEmpty() && (n++ % 10) == 0) {
            ResourceLocation key = BuiltInRegistries.ITEM.getKey(st.getItem());
            String px = PIXELS.computeIfAbsent(key.toString(), k -> read(mc, new ResourceLocation(key.getNamespace(), "textures/item/" + key.getPath() + ".png")));
            if (!px.isEmpty()) { j.addProperty("itemKey", key.toString()); j.addProperty("itemPx", px); }
        }
        return j;
    }

    private static String read(Minecraft mc, ResourceLocation tex) {
        try {
            Optional<Resource> r = mc.getResourceManager().getResource(tex);
            if (r.isEmpty()) return "";
            try (InputStream in = r.get().open(); NativeImage img = NativeImage.read(in)) {
                int w = img.getWidth(), h = img.getHeight();
                if (w != h || w > 32) return "";
                StringBuilder sb = new StringBuilder(w * h * 8 + 4);
                sb.append(String.format("%02x", w));
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    int abgr = img.getPixelRGBA(x, y);
                    int a = (abgr >>> 24) & 255, b = (abgr >>> 16) & 255, g = (abgr >>> 8) & 255, rr = abgr & 255;
                    sb.append(String.format("%02x%02x%02x%02x", rr, g, b, a));
                }
                return sb.toString();
            }
        } catch (Exception ex) { return ""; }
    }
}
