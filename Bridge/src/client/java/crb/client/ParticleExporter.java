package crb.client;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.vertex.VertexConsumer;
import crb.Bin;
import crb.Endpoint;
import crb.Wire;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleRenderType;
import net.minecraft.client.particle.SingleQuadParticle;
import net.minecraft.world.phys.Vec3;

import java.util.Map;
import java.util.Queue;

/**
 * Snapshots live Java particles each tick by asking each sprite particle to emit its real quad into a recorder:
 * Java owns position, sprite UVs (including age-animated sprites), colour, size, lifetime and motion.
 */
public final class ParticleExporter {
    public static final int MAX = 768, STRIDE = 48;
    public static final String BLOCK_ATLAS = "minecraft:textures/atlas/blocks.png";
    public static final String PARTICLE_ATLAS = "minecraft:textures/atlas/particles.png";
    public long batches, exported, skipped;

    private static final class Quad implements VertexConsumer {
        final float[] p = new float[12]; float u0 = 1, u1 = 0, v0 = 1, v1 = 0; int color, light, n;
        void reset() { n = 0; u0 = 1; u1 = 0; v0 = 1; v1 = 0; }
        @Override public VertexConsumer vertex(double x, double y, double z) { if (n < 4) { p[n * 3] = (float) x; p[n * 3 + 1] = (float) y; p[n * 3 + 2] = (float) z; } return this; }
        @Override public VertexConsumer color(int r, int g, int b, int a) { color = (a & 255) << 24 | (r & 255) << 16 | (g & 255) << 8 | (b & 255); return this; }
        @Override public VertexConsumer uv(float u, float v) { u0 = Math.min(u0, u); u1 = Math.max(u1, u); v0 = Math.min(v0, v); v1 = Math.max(v1, v); return this; }
        @Override public VertexConsumer overlayCoords(int a, int b) { return this; }
        @Override public VertexConsumer uv2(int a, int b) { light = (b << 16) | a; return this; }
        @Override public VertexConsumer normal(float a, float b, float c) { return this; }
        @Override public void endVertex() { n++; }
        @Override public void defaultColor(int r, int g, int b, int a) { }
        @Override public void unsetDefaultColor() { }
    }

    public void tick(Minecraft mc, Endpoint endpoint, TextureExporter textures) {
        if (mc.level == null || mc.player == null) return;
        Camera camera = mc.gameRenderer.getMainCamera();
        Vec3 cam = camera.getPosition();
        Map<ParticleRenderType, Queue<Particle>> all = mc.particleEngine.particles;
        Bin bin = new Bin(4096, MAX * STRIDE);
        Quad q = new Quad();
        int count = 0, blockSheet = 0;
        for (Map.Entry<ParticleRenderType, Queue<Particle>> e : all.entrySet()) {
            ParticleRenderType type = e.getKey();
            int sheet;
            if (type == ParticleRenderType.TERRAIN_SHEET) sheet = 0;
            else if (type == ParticleRenderType.PARTICLE_SHEET_OPAQUE || type == ParticleRenderType.PARTICLE_SHEET_TRANSLUCENT || type == ParticleRenderType.PARTICLE_SHEET_LIT) sheet = 1;
            else { skipped += e.getValue().size(); continue; }
            for (Particle particle : e.getValue()) {
                if (count >= MAX) { skipped++; continue; }
                if (!(particle instanceof SingleQuadParticle sq)) { skipped++; continue; }
                q.reset();
                sq.render(q, camera, 1.0f);
                if (q.n != 4) { skipped++; continue; }
                float cx = (q.p[0] + q.p[3] + q.p[6] + q.p[9]) / 4f, cy = (q.p[1] + q.p[4] + q.p[7] + q.p[10]) / 4f, cz = (q.p[2] + q.p[5] + q.p[8] + q.p[11]) / 4f;
                if (cx * cx + cy * cy + cz * cz > 48 * 48) { skipped++; continue; }
                float size = sq.getQuadSize(1.0f);
                if (!Float.isFinite(cx + cy + cz + size)) { skipped++; continue; }
                bin.u32(System.identityHashCode(particle));
                bin.f32(cx).f32(cy).f32(cz).f32(size);
                bin.f32(q.u0).f32(q.v0).f32(q.u1).f32(q.v1);
                bin.u32(q.color);
                int block = (q.light & 0xFFFF) >> 4, sky = (q.light >>> 16) >> 4;
                bin.u8(sheet).u8(Math.min(15, block)).u8(Math.min(15, sky)).u8(0);
                bin.f32(0f);
                if (sheet == 0) blockSheet++;
                count++;
            }
        }
        textures.request(new net.minecraft.resources.ResourceLocation(PARTICLE_ATLAS));
        JsonObject j = new JsonObject();
        j.addProperty("tick", mc.level.getGameTime());
        j.addProperty("cx", cam.x); j.addProperty("cy", cam.y); j.addProperty("cz", cam.z);
        j.addProperty("count", count); j.addProperty("stride", STRIDE);
        j.addProperty("sheet0", BLOCK_ATLAS); j.addProperty("sheet1", PARTICLE_ATLAS);
        endpoint.latest(Endpoint.LANE_PARTICLES, Wire.PARTICLES, j.toString(), bin.toArray());
        batches++; exported += count;
    }
}
