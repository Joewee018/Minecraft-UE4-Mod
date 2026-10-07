package crb.client;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.systems.RenderSystem;
import crb.Endpoint;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.packs.resources.Resource;
import org.lwjgl.opengl.GL11;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Deque;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

/**
 * Sends textures the host needs (atlases, skins, armor, GUI sheets, item icons) as bounded PNG chunks on the
 * texture lane. Failures are recorded per texture and never stop pose, state or world frames.
 */
public final class TextureExporter {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    public static final int CHUNK = 192 * 1024;
    public static final int MAX_BYTES = 64 * 1024 * 1024; // mod resource packs (Guns++) grow the block atlas to 8192x4096
    public static final int MAX_DIM = 8192;
    public static final int MAX_TRACKED = 192;

    private static final class Entry {
        final String name; final ResourceLocation location; int generation;
        byte[] png; int width, height, offset; boolean done, failed; String error = "";
        byte[] dynamicPng; boolean linear;
        Entry(String name, ResourceLocation location) { this.name = name; this.location = location; }
    }

    private final Map<String, Entry> entries = new HashMap<>();
    private final Deque<Entry> queue = new ArrayDeque<>();
    private int generationCounter = 1;
    public long sent, failed, bytesSent;
    public String lastError = "";

    public void reset() {
        entries.clear(); queue.clear(); generationCounter++;
    }

    public void request(ResourceLocation location) {
        if (location == null) return;
        String name = location.toString();
        if (entries.containsKey(name)) return;
        if (entries.size() >= MAX_TRACKED) { failed++; lastError = "texture table full, skipped " + name; return; }
        Entry e = new Entry(name, location);
        e.generation = generationCounter++;
        entries.put(name, e);
        queue.add(e);
    }

    /** Replace a generated texture (e.g. the item icon sheet). */
    public void publish(String name, byte[] png, boolean linear) {
        Entry e = entries.get(name);
        if (e == null) { e = new Entry(name, null); entries.put(name, e); }
        queue.remove(e);
        e.generation = generationCounter++;
        e.dynamicPng = png; e.png = null; e.offset = 0; e.done = false; e.failed = false; e.linear = linear;
        queue.add(e);
    }

    public int generationOf(String name) { Entry e = entries.get(name); return e == null || !e.done ? 0 : e.generation; }
    public boolean isFailed(String name) { Entry e = entries.get(name); return e != null && e.failed; }
    public int pending() { return queue.size(); }

    public void tick(Minecraft mc, Endpoint endpoint) {
        int budget = 4;
        while (budget > 0 && !queue.isEmpty() && endpoint.textureFree() > 0) {
            Entry e = queue.peek();
            if (e.png == null) {
                try {
                    e.png = e.dynamicPng != null ? e.dynamicPng : produce(mc, e.location);
                    e.dynamicPng = null;
                    if (e.png.length > MAX_BYTES) throw new IllegalStateException("PNG " + e.png.length + " bytes exceeds " + MAX_BYTES);
                    e.width = be32(e.png, 16); e.height = be32(e.png, 20);
                    if (e.png.length < 24 || e.png[1] != 'P' || e.width <= 0 || e.height <= 0 || e.width > MAX_DIM || e.height > MAX_DIM)
                        throw new IllegalStateException("not a usable PNG (" + e.width + "x" + e.height + ")");
                    e.offset = 0;
                } catch (Exception ex) {
                    queue.poll();
                    e.failed = true; e.error = ex.toString(); failed++; lastError = e.name + ": " + ex.getMessage();
                    LOG.warn("Texture {} could not be exported ({}); poses and world updates continue with a fallback material.", e.name, ex.getMessage());
                    continue;
                }
            }
            int len = Math.min(CHUNK, e.png.length - e.offset);
            JsonObject j = new JsonObject();
            j.addProperty("name", e.name); j.addProperty("gen", e.generation);
            j.addProperty("w", e.width); j.addProperty("h", e.height);
            j.addProperty("total", e.png.length); j.addProperty("offset", e.offset);
            j.addProperty("last", e.offset + len >= e.png.length);
            if (e.linear) j.addProperty("linear", true);
            if (!endpoint.texture(j.toString(), Arrays.copyOfRange(e.png, e.offset, e.offset + len))) break;
            e.offset += len; bytesSent += len; budget--;
            if (e.offset >= e.png.length) { queue.poll(); e.done = true; e.png = null; sent++; }
        }
    }

    private static int be32(byte[] b, int o) {
        return ((b[o] & 255) << 24) | ((b[o + 1] & 255) << 16) | ((b[o + 2] & 255) << 8) | (b[o + 3] & 255);
    }

    /** Client (render) thread only: may read back GL textures. */
    static byte[] produce(Minecraft mc, ResourceLocation location) throws Exception {
        RenderSystem.assertOnRenderThread();
        AbstractTexture texture = mc.getTextureManager().getTexture(location, null);
        boolean generated = texture instanceof DynamicTexture || texture instanceof TextureAtlas;
        if (!generated) {
            Optional<Resource> res = mc.getResourceManager().getResource(location);
            if (res.isPresent()) {
                try (InputStream in = res.get().open()) {
                    byte[] bytes = in.readNBytes(MAX_BYTES + 1);
                    if (bytes.length > MAX_BYTES) throw new IllegalStateException("resource larger than " + MAX_BYTES);
                    return bytes;
                }
            }
        }
        if (texture == null) throw new IllegalStateException("texture is not loaded");
        return readBack(texture.getId());
    }

    static byte[] readBack(int glId) throws Exception {
        RenderSystem.bindTexture(glId);
        int w = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);
        int h = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_HEIGHT);
        if (w <= 0 || h <= 0 || w > MAX_DIM || h > MAX_DIM) { RenderSystem.bindTexture(0); throw new IllegalStateException("GL texture size " + w + "x" + h); }
        try (NativeImage img = new NativeImage(w, h, false)) {
            img.downloadTexture(0, false);
            return img.asByteArray();
        } finally {
            RenderSystem.bindTexture(0);
        }
    }
}
