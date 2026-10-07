package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.NativeImage;
import net.minecraft.client.Minecraft;
import net.minecraft.world.item.ItemStack;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * Zombies wall-buys as chalk drawings: renders each wall gun with Minecraft's own item renderer (ItemIcons), traces the
 * silhouette into a white, slightly broken chalk outline (the item itself is not drawn) and publishes the sheet as the
 * runtime texture "crb:chalk". Unreal draws one quad per wall-buy on the wall with that sheet. Render thread only.
 */
public final class ChalkWallBuys {
    public static final String SHEET = "crb:chalk";
    private String signature = "";

    public JsonArray export(Minecraft mc, TextureExporter textures) {
        List<crb.zm.ZombiesGame.WallBuy> list = crb.zm.ZombiesGame.WALLBUYS;
        JsonArray arr = new JsonArray();
        if (list.isEmpty()) { signature = ""; return arr; }
        StringBuilder sig = new StringBuilder();
        for (var w : list) sig.append(w.gunId()).append('@').append(w.x()).append(',').append(w.y()).append(',').append(w.z()).append(';');
        if (!sig.toString().equals(signature)) {
            try {
                List<ItemStack> stacks = new ArrayList<>();
                for (var w : list) stacks.add(w.stack());
                byte[] png = ItemIcons.render(mc, stacks);
                textures.publish(SHEET, outline(png), false);
                signature = sig.toString();
            } catch (Exception ignored) { }
        }
        int i = 0;
        for (var w : list) {
            JsonObject o = new JsonObject();
            o.addProperty("x", w.x()); o.addProperty("y", w.y()); o.addProperty("z", w.z()); o.addProperty("face", w.face());
            o.addProperty("cell", i++);
            arr.add(o);
        }
        return arr;
    }

    /** White chalk outline around the opaque silhouette (1-2 px), with chalky gaps; the interior stays empty. */
    static byte[] outline(byte[] png) throws Exception {
        try (NativeImage src = NativeImage.read(new java.io.ByteArrayInputStream(png)); NativeImage out = new NativeImage(src.getWidth(), src.getHeight(), true)) {
            int w = src.getWidth(), h = src.getHeight();
            Random r = new Random(1234);
            out.fillRect(0, 0, w, h, 0);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                if (alpha(src, x, y) >= 40) continue;
                int near = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if ((dx != 0 || dy != 0) && xx >= 0 && yy >= 0 && xx < w && yy < h && (xx / 32 == x / 32) && (yy / 32 == y / 32) && alpha(src, xx, yy) >= 40) near++;
                }
                if (near == 0 || r.nextFloat() < 0.08f) continue;
                int a = 170 + r.nextInt(86), v = 225 + r.nextInt(31);
                out.setPixelRGBA(x, y, (a << 24) | (v << 16) | (v << 8) | v); // ABGR
            }
            return out.asByteArray();
        }
    }

    static int alpha(NativeImage img, int x, int y) { return (img.getPixelRGBA(x, y) >>> 24) & 255; }
}
