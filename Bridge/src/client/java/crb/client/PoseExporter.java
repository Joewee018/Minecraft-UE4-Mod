package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.mojang.blaze3d.vertex.PoseStack;
import crb.Bin;
import crb.Endpoint;
import crb.Wire;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.renderer.LightTexture;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Exports fresh Java pose frames every client tick: the real PlayerRenderer output (third person body, armor,
 * held items) and the real first-person ItemInHandRenderer output. Geometry is sent regardless of whether any
 * referenced texture has been (or can be) exported — the texture lane is independent.
 */
public final class PoseExporter {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    public static final int STRIDE = 28;
    private long seq;
    public long frames, errors, overflowVertices;
    public String lastError = "";
    public float lastWalkPos, lastWalkSpeed;
    public int lastVertices;
    public long entityErrors;
    public static final int MAX_ENTITIES = 48;
    public static final double MAX_ENTITY_DIST = 64.0;

    public long seq() { return seq; }

    public void tick(Minecraft mc, Endpoint endpoint, TextureExporter textures, int epoch) {
        LocalPlayer p = mc.player;
        if (p == null || mc.level == null) return;
        // The entity dispatcher has no camera until the first level frame renders (name tags read it).
        if (mc.getEntityRenderDispatcher().camera == null) return;
        try {
            Capture cap = new Capture();
            float pt = 1.0f;
            // Third-person body: real PlayerRenderer, identity pose = feet-relative, world-aligned axes.
            cap.beginGroup(0);
            EntityRenderer<? super LocalPlayer> renderer = mc.getEntityRenderDispatcher().getRenderer(p);
            float yaw = Mth.lerp(pt, p.yRotO, p.getYRot());
            renderer.render(p, yaw, pt, new PoseStack(), cap, LightTexture.FULL_BRIGHT);
            // First-person hands and held item: real ItemInHandRenderer, OpenGL view space.
            cap.beginGroup(1);
            mc.gameRenderer.itemInHandRenderer.renderHandsWithItems(pt, new PoseStack(), cap, p, LightTexture.FULL_BRIGHT);
            // World entities (mobs, items, armor stands - e.g. From The Fog's Herobrine rig, projectiles): the real
            // EntityRenderDispatcher output, feet-relative to the player like group 0. Bounded by count and by the
            // shared vertex budget (they are captured last, so overflow never costs the player's own pose).
            cap.beginGroup(2);
            var dispatcher = mc.getEntityRenderDispatcher();
            List<Entity> near = new ArrayList<>();
            for (Entity e : mc.level.entitiesForRendering()) {
                if (e == p || e.isRemoved() || e.distanceToSqr(p) > MAX_ENTITY_DIST * MAX_ENTITY_DIST) continue;
                near.add(e);
            }
            near.sort(Comparator.comparingDouble(e -> e.distanceToSqr(p)));
            JsonArray ents = new JsonArray();
            for (int i = 0; i < near.size() && i < MAX_ENTITIES; i++) {
                Entity e = near.get(i);
                try {
                    dispatcher.render(e, e.getX() - p.getX(), e.getY() - p.getY(), e.getZ() - p.getZ(), e.getYRot(), pt, new PoseStack(), cap, dispatcher.getPackedLightCoords(e, pt));
                    JsonObject eo = new JsonObject();
                    eo.addProperty("type", BuiltInRegistries.ENTITY_TYPE.getKey(e.getType()).toString());
                    eo.addProperty("x", e.getX()); eo.addProperty("y", e.getY()); eo.addProperty("z", e.getZ());
                    ents.add(eo);
                } catch (Throwable t) { entityErrors++; lastError = "entity " + e.getType() + ": " + t; }
            }

            Bin bin = new Bin(cap.total * STRIDE + 64, Capture.MAX_VERTICES * STRIDE);
            JsonArray groups = new JsonArray();
            JsonArray debug = new JsonArray();
            for (Capture.Surface s : cap.surfaces) {
                int count = s.vertices - (s.vertices % 4);
                if (count <= 0) continue;
                JsonObject g = new JsonObject();
                g.addProperty("g", s.group); g.addProperty("tex", s.texture); g.addProperty("layer", s.layer); g.addProperty("count", count);
                groups.add(g);
                if (!s.texture.isEmpty()) textures.request(new ResourceLocation(s.texture));
                float[] d = s.data;
                // Debug summary per surface: bounds of positions and UVs as exported (compared against Unreal).
                float[] mn = { Float.MAX_VALUE, Float.MAX_VALUE, Float.MAX_VALUE, Float.MAX_VALUE, Float.MAX_VALUE };
                float[] mx = { -Float.MAX_VALUE, -Float.MAX_VALUE, -Float.MAX_VALUE, -Float.MAX_VALUE, -Float.MAX_VALUE };
                for (int i = 0; i < count; i++) for (int k = 0; k < 5; k++) { mn[k] = Math.min(mn[k], d[i * 9 + k]); mx[k] = Math.max(mx[k], d[i * 9 + k]); }
                JsonObject dg = g.deepCopy();
                JsonArray b = new JsonArray(); for (int k = 0; k < 5; k++) { b.add(mn[k]); b.add(mx[k]); }
                dg.add("xyzuvMinMax", b);
                debug.add(dg);
                for (int i = 0; i < count; i++) {
                    int o = i * 9;
                    bin.f32(d[o]).f32(d[o + 1]).f32(d[o + 2]).f32(d[o + 3]).f32(d[o + 4]);
                    bin.u32(Float.floatToRawIntBits(d[o + 5]));
                    bin.u8(Math.round(Mth.clamp(d[o + 6], -1, 1) * 127)).u8(Math.round(Mth.clamp(d[o + 7], -1, 1) * 127)).u8(Math.round(Mth.clamp(d[o + 8], -1, 1) * 127)).u8(0);
                }
            }
            overflowVertices += cap.overflow;
            DebugCapture.lastSurfaces = debug;
            JsonObject j = new JsonObject();
            j.addProperty("seq", ++seq);
            j.addProperty("tick", mc.level.getGameTime());
            j.addProperty("epoch", epoch);
            j.addProperty("x", p.getX()); j.addProperty("y", p.getY()); j.addProperty("z", p.getZ());
            lastWalkPos = p.walkAnimation.position();
            lastWalkSpeed = p.walkAnimation.speed();
            j.addProperty("walkPos", lastWalkPos);
            j.addProperty("walkSpeed", lastWalkSpeed);
            j.addProperty("attack", p.getAttackAnim(pt));
            // Vanilla view-bobbing inputs (GameRenderer.bobView), so Unreal can bob camera + hands identically.
            j.addProperty("yaw", p.getYRot()); j.addProperty("pitch", p.getXRot());
            j.addProperty("walkDist", p.walkDist); j.addProperty("walkDistO", p.walkDistO);
            j.addProperty("bob", p.bob); j.addProperty("oBob", p.oBob);
            j.addProperty("using", p.isUsingItem());
            j.addProperty("mainhand", BuiltInRegistries.ITEM.getKey(p.getMainHandItem().getItem()).toString());
            j.addProperty("overflow", cap.overflow);
            j.add("groups", groups);
            j.add("entities", ents);
            lastVertices = bin.size() / STRIDE;
            endpoint.latest(Endpoint.LANE_POSE, Wire.POSE, j.toString(), bin.toArray());
            frames++;
        } catch (Throwable t) {
            errors++;
            lastError = t.toString();
            if (errors < 5 || errors % 200 == 0) LOG.warn("Pose capture failed (frame skipped, next tick retries): {}", t.toString());
        }
    }
}
