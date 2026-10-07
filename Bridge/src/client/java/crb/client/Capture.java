package crb.client;

import com.mojang.blaze3d.vertex.BufferBuilder;
import com.mojang.blaze3d.vertex.VertexConsumer;
import net.minecraft.client.renderer.MultiBufferSource;
import net.minecraft.client.renderer.RenderType;
import net.minecraft.resources.ResourceLocation;

import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * A MultiBufferSource that records the vertices Minecraft's real renderers emit instead of drawing them.
 * No GL draw happens. Each RenderType becomes one surface tagged with its texture and translucency.
 */
public final class Capture extends MultiBufferSource.BufferSource {
    public static final int MAX_VERTICES = 36000;
    private static final Map<RenderType, String[]> INFO = new IdentityHashMap<>();

    public static final class Surface implements VertexConsumer {
        public final int group; public final String texture; public final int layer; public final boolean skip;
        public float[] data = new float[64 * 9]; // x y z u v color(asFloatBits) nx ny nz
        public int vertices;
        private final Capture owner;
        private float x, y, z, u, v, nx, ny = 1, nz;
        private int color = 0xFFFFFFFF, defaultColor = 0xFFFFFFFF;
        private boolean hasDefault, hasColor;

        Surface(Capture owner, int group, String texture, int layer, boolean skip) {
            this.owner = owner; this.group = group; this.texture = texture; this.layer = layer; this.skip = skip;
        }
        @Override public VertexConsumer vertex(double a, double b, double c) { x = (float) a; y = (float) b; z = (float) c; return this; }
        @Override public VertexConsumer color(int r, int g, int b, int a) { color = (a & 255) << 24 | (r & 255) << 16 | (g & 255) << 8 | (b & 255); hasColor = true; return this; }
        @Override public VertexConsumer uv(float a, float b) { u = a; v = b; return this; }
        @Override public VertexConsumer overlayCoords(int a, int b) { return this; }
        @Override public VertexConsumer uv2(int a, int b) { return this; }
        @Override public VertexConsumer normal(float a, float b, float c) { nx = a; ny = b; nz = c; return this; }
        @Override public void defaultColor(int r, int g, int b, int a) { defaultColor = (a & 255) << 24 | (r & 255) << 16 | (g & 255) << 8 | (b & 255); hasDefault = true; }
        @Override public void unsetDefaultColor() { hasDefault = false; }
        @Override public void endVertex() {
            int c = hasDefault ? defaultColor : (hasColor ? color : 0xFFFFFFFF);
            hasColor = false;
            if (skip) return;
            if (owner.total >= MAX_VERTICES || !Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(z)) { owner.overflow++; return; }
            if (vertices * 9 + 9 > data.length) data = java.util.Arrays.copyOf(data, data.length * 2);
            int o = vertices * 9;
            data[o] = x; data[o + 1] = y; data[o + 2] = z; data[o + 3] = u; data[o + 4] = v;
            data[o + 5] = Float.intBitsToFloat(c); data[o + 6] = nx; data[o + 7] = ny; data[o + 8] = nz;
            vertices++; owner.total++;
        }
    }

    public final List<Surface> surfaces = new ArrayList<>();
    private final Map<RenderType, Surface> current = new IdentityHashMap<>();
    public int group;
    public int total, overflow;

    private static BufferBuilder shared;

    private static BufferBuilder shared() {
        // One native buffer for the lifetime of the client; Capture never draws, so it is never written.
        if (shared == null) shared = new BufferBuilder(256);
        return shared;
    }

    public Capture() { super(shared(), Map.of()); }

    public void beginGroup(int g) { group = g; current.clear(); }

    @Override
    public VertexConsumer getBuffer(RenderType type) {
        Surface s = current.get(type);
        if (s == null) {
            String[] info = info(type);
            boolean skip = info[2] != null;
            s = new Surface(this, group, info[0], "1".equals(info[1]) ? 1 : 0, skip);
            current.put(type, s);
            if (!skip) surfaces.add(s);
        }
        return s;
    }

    @Override public void endBatch() { }
    @Override public void endBatch(RenderType type) { }
    @Override public void endLastBatch() { }

    /** [texture, translucent?"1":"0", skipReason or null] cached per RenderType instance. */
    static String[] info(RenderType type) {
        synchronized (INFO) {
            return INFO.computeIfAbsent(type, t -> {
                String desc = t.toString();
                String name = desc.startsWith("RenderType[") ? desc.substring(11, Math.max(11, desc.indexOf(':', 11))) : desc;
                String texture = "";
                if (t instanceof RenderType.CompositeRenderType c) {
                    Optional<ResourceLocation> tex = c.state().textureState.cutoutTexture();
                    if (tex.isPresent()) texture = tex.get().toString();
                }
                String skip = null;
                if (name.contains("glint") || name.contains("outline") || name.contains("shadow") || name.contains("lines") || name.contains("text") || name.contains("leash") || name.contains("water_mask"))
                    skip = name;
                boolean translucent = name.contains("translucent");
                return new String[] { texture, translucent ? "1" : "0", skip };
            });
        }
    }
}
