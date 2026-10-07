package crb.pp;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.particles.DustParticleOptions;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.chat.Component;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3f;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Physics & Portal mod - Portal A / B (glPortal-style concepts, own implementation). A portal is a 1 x 2 block opening
 * drawn on a solid block face: centre, outward normal n, up u, right r = u x n. Traversal maps the local frame of the
 * entry portal onto the exit portal turned around (r -> -r', u -> u', n -> -n'), so position, velocity (momentum) and
 * facing come out the other side transformed, never reset. Server thread owns the portals and teleports non-player
 * entities (mobs, items, arrows, the mod's test physics objects); the local player's controller (client thread) reads
 * the immutable snapshot and teleports the player inside its own movement step.
 */
public final class Portals {
    public static final Portals INSTANCE = new Portals();
    public static final String TEST_TAG = "crb_pp_object";
    public static final double HALF_W = 0.5, HALF_H = 1.0;

    /** Immutable portal (safe to read on any thread). */
    public record Portal(int id, Vec3 center, Direction normal, Direction up, BlockPos block, String dim) {
        public Vec3 n() { return Vec3.atLowerCornerOf(normal.getNormal()); }
        public Vec3 u() { return Vec3.atLowerCornerOf(up.getNormal()); }
        public Vec3 r() { return u().cross(n()); }
        /** Local (right, up, out) coordinates of a world point. */
        public Vec3 local(Vec3 p) { Vec3 d = p.subtract(center); return new Vec3(d.dot(r()), d.dot(u()), d.dot(n())); }
        public boolean inside(Vec3 local, double margin) { return Math.abs(local.x) <= HALF_W + margin && Math.abs(local.y) <= HALF_H + margin; }
    }
    public static volatile Portal[] SNAPSHOT = new Portal[2];
    public static volatile boolean ENABLED;
    private final Portal[] portals = new Portal[2];
    private final Map<UUID, Long> cooldown = new ConcurrentHashMap<>();
    /** Velocity each nearby entity had last tick: vanilla collision against the wall behind a portal zeroes the
     *  current velocity before this tick runs, so contact + the pre-collision velocity is the trigger. */
    private final Map<UUID, Vec3> lastVel = new HashMap<>();
    private final List<UUID> testObjects = new ArrayList<>();
    private long tick, teleports;
    public static volatile long ENTITY_TELEPORTS;

    // ================================================================================================= transforms
    /** Map a world position through portal a -> b. */
    public static Vec3 mapPoint(Portal a, Portal b, Vec3 p) {
        Vec3 l = a.local(p);
        return b.center.add(b.r().scale(-l.x)).add(b.u().scale(l.y)).add(b.n().scale(-l.z));
    }
    /** Where a body (centre + half extents) entering a comes out of b: same spot in the opening, clear of the surface. */
    public static Vec3 exitCenter(Portal a, Portal b, Vec3 center, double halfW, double halfH) {
        Vec3 l = a.local(center), n = b.n();
        double ext = Math.abs(n.x) * halfW + Math.abs(n.y) * halfH + Math.abs(n.z) * halfW;
        double lx = Math.max(-HALF_W, Math.min(HALF_W, l.x)), ly = Math.max(-HALF_H, Math.min(HALF_H, l.y));
        if (b.normal.getAxis().isVertical() || a.normal.getAxis().isVertical()) { lx = 0; ly = 0; }   // floor / ceiling: come out centred
        return b.center.add(b.r().scale(-lx)).add(b.u().scale(ly)).add(n.scale(ext + 0.2));
    }

    /** Map a direction / velocity through portal a -> b (momentum preserved, orientation transformed). */
    public static Vec3 mapDir(Portal a, Portal b, Vec3 v) {
        double x = v.dot(a.r()), y = v.dot(a.u()), z = v.dot(a.n());
        return b.r().scale(-x).add(b.u().scale(y)).add(b.n().scale(-z));
    }
    public static float mapYaw(Portal a, Portal b, float yaw) {
        double r = Math.toRadians(yaw);
        Vec3 f = mapDir(a, b, new Vec3(-Math.sin(r), 0, Math.cos(r)));
        if (f.x * f.x + f.z * f.z < 1e-6) return yaw;                       // through a floor / ceiling: keep the heading
        return (float) Math.toDegrees(Math.atan2(-f.x, f.z));
    }
    public static Portal other(Portal p) { Portal[] s = SNAPSHOT; return p == null || s[0] == null || s[1] == null ? null : s[1 - p.id]; }

    /**
     * Traversal test used for the player and every entity: the body (centre + half extents) touches the portal surface
     * from the front, its centre is inside the 1 x 2 opening and it moves into the portal. Solid blocks sit behind every
     * portal, so a body can never pass the plane by itself - contact while moving in is the trigger.
     */
    public static Portal entering(Vec3 center, Vec3 vel, double halfW, double halfH) {
        Portal[] s = SNAPSHOT;
        if (!ENABLED || s[0] == null || s[1] == null) return null;
        for (Portal p : s) {
            Vec3 n = p.n();
            if (vel.dot(n) > -0.02) continue;
            Vec3 l = p.local(center);
            double ext = Math.abs(n.x) * halfW + Math.abs(n.y) * halfH + Math.abs(n.z) * halfW;
            if (l.z - ext > 0.18 || l.z < -0.6) continue;
            if (Math.abs(l.x) <= HALF_W + 0.15 && Math.abs(l.y) <= HALF_H + 0.15) return p;
        }
        return null;
    }

    // ================================================================================================= placement
    /** Validate and place portal `which` on the face at `hitPos` / `face` of `block`. */
    public String place(ServerLevel l, int which, BlockPos block, Direction face, float playerYaw) {
        if (which < 0 || which > 1) return "bad portal id";
        if (!okFace(l, block, face)) return "portal needs a solid face with room in front";
        // the 1 x 2 opening runs up a wall, or along the view direction on a floor / ceiling
        Direction up = face.getAxis().isHorizontal() ? Direction.UP : Direction.fromYRot(playerYaw);
        BlockPos lower;
        if (okFace(l, block.relative(up), face)) lower = block;
        else if (okFace(l, block.relative(up.getOpposite()), face)) lower = block.relative(up.getOpposite());
        else return "portal needs a 1 x 2 solid surface";
        BlockPos upper = lower.relative(up);
        Vec3 c = Vec3.atCenterOf(lower).add(Vec3.atCenterOf(upper)).scale(0.5).add(Vec3.atLowerCornerOf(face.getNormal()).scale(0.51));
        Portal p = new Portal(which, c, face, up, lower, l.dimension().location().toString());
        Portal o = portals[1 - which];
        if (o != null && o.center.distanceTo(c) < 1.2) return "overlaps the other portal";
        portals[which] = p; publish();
        l.playSound(null, c.x, c.y, c.z, SoundEvents.ENDER_EYE_DEATH, SoundSource.PLAYERS, 0.8f, which == 0 ? 1.4f : 0.9f);
        ring(l, p);
        return "portal " + (which == 0 ? "A" : "B") + " at " + lower.toShortString() + " " + face.getName();
    }

    private static boolean okFace(ServerLevel l, BlockPos b, Direction face) {
        BlockPos front = b.relative(face);
        return l.getBlockState(b).isFaceSturdy(l, b, face) && l.getBlockState(front).getCollisionShape(l, front).isEmpty();
    }

    /** Portal gun: ray from the eye, place on the face hit. */
    public String shoot(ServerPlayer p, int which) {
        Vec3 eye = p.getEyePosition(), end = eye.add(p.getLookAngle().scale(64));
        BlockHitResult h = p.level().clip(new ClipContext(eye, end, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, p));
        if (h.getType() != HitResult.Type.BLOCK) return "no surface";
        return place(p.serverLevel(), which, h.getBlockPos(), h.getDirection(), p.getYRot());
    }

    public void clear() { portals[0] = portals[1] = null; publish(); }

    private void publish() { SNAPSHOT = new Portal[] { portals[0], portals[1] }; }

    private void ring(ServerLevel l, Portal p) {
        DustParticleOptions d = new DustParticleOptions(p.id == 0 ? new Vector3f(0.2f, 0.55f, 1f) : new Vector3f(1f, 0.55f, 0.1f), 1f);
        for (int i = 0; i < 24; i++) {
            double a = Math.PI * 2 * i / 24;
            Vec3 q = p.center.add(p.r().scale(Math.cos(a) * HALF_W)).add(p.u().scale(Math.sin(a) * HALF_H));
            l.sendParticles(d, q.x, q.y, q.z, 1, 0, 0, 0, 0);
        }
    }

    // ================================================================================================= entities
    public void tick(MinecraftServer server) {
        tick++;
        if (!ENABLED) return;
        Portal[] s = SNAPSHOT;
        if (s[0] == null || s[1] == null) { lastVel.clear(); return; }
        ServerLevel l = server.overworld();
        Map<UUID, Vec3> seen = new HashMap<>();
        for (Portal p : s) {
            if (!p.dim.equals(l.dimension().location().toString())) continue;
            AABB box = new AABB(p.center, p.center).inflate(3);
            for (Entity e : l.getEntities((Entity) null, box, e -> !(e instanceof Player) && e.isAlive() && !e.isPassenger())) {
                Vec3 cur = e.getDeltaMovement();
                Vec3 prev = lastVel.get(e.getUUID());
                seen.put(e.getUUID(), cur.lengthSqr() > 1e-6 ? cur : prev != null ? prev : cur);
                Long cd = cooldown.get(e.getUUID());
                if (cd != null && tick - cd < 4) continue;
                Vec3 c = e.position().add(0, e.getBbHeight() * 0.5, 0);
                Vec3 v = cur;
                Portal hit = entering(c, v, e.getBbWidth() * 0.5, e.getBbHeight() * 0.5);
                if (hit == null && prev != null) { v = prev; hit = entering(c, v, e.getBbWidth() * 0.5, e.getBbHeight() * 0.5); }
                if (hit != p) continue;
                seen.remove(e.getUUID());
                Portal o = other(p);
                Vec3 nv = mapDir(p, o, v);
                Vec3 at = exitCenter(p, o, c, e.getBbWidth() * 0.5, e.getBbHeight() * 0.5).subtract(0, e.getBbHeight() * 0.5, 0);
                e.teleportTo(at.x, at.y, at.z);
                e.setDeltaMovement(nv);
                e.hurtMarked = true;
                e.setYRot(mapYaw(p, o, e.getYRot()));
                cooldown.put(e.getUUID(), tick);
                teleports++; ENTITY_TELEPORTS = teleports;
                l.playSound(null, at.x, at.y, at.z, SoundEvents.ENDERMAN_TELEPORT, SoundSource.NEUTRAL, 0.4f, 1.6f);
            }
        }
        lastVel.clear(); lastVel.putAll(seen);
        if (tick % 10 == 0) for (Portal p : s) ring(l, p);
    }

    /** Debug: a Minecraft physics object (slime block item entity) thrown from the player with velocity. */
    public String spawnObject(ServerPlayer p, double speed) {
        ItemStack st = new ItemStack(Items.SLIME_BLOCK);
        CompoundTag t = new CompoundTag(); t.putBoolean("pp", true); st.getOrCreateTag().put(TEST_TAG, t);
        st.setHoverName(Component.literal("Physics Cube"));
        Vec3 eye = p.getEyePosition(), d = p.getLookAngle();
        ItemEntity e = new ItemEntity(p.serverLevel(), eye.x + d.x, eye.y + d.y - 0.2, eye.z + d.z, st, d.x * speed / 20, d.y * speed / 20 + 0.1, d.z * speed / 20);
        e.setNeverPickUp(); e.setUnlimitedLifetime();
        p.serverLevel().addFreshEntity(e);
        testObjects.add(e.getUUID());
        return e.getUUID().toString();
    }

    public void cleanup(MinecraftServer server) {
        for (UUID id : testObjects) for (ServerLevel l : server.getAllLevels()) { Entity e = l.getEntity(id); if (e != null) e.discard(); }
        testObjects.clear(); cooldown.clear(); clear();
        PPProps.clear();
    }

    public JsonObject json() {
        JsonObject j = new JsonObject(); JsonArray a = new JsonArray();
        Portal[] s = SNAPSHOT;
        for (Portal p : s) {
            if (p == null) { a.add(new JsonObject()); continue; }
            JsonObject o = new JsonObject();
            o.addProperty("id", p.id); o.addProperty("x", p.center.x); o.addProperty("y", p.center.y); o.addProperty("z", p.center.z);
            Vec3 n = p.n(), u = p.u();
            o.addProperty("nx", n.x); o.addProperty("ny", n.y); o.addProperty("nz", n.z); o.addProperty("ux", u.x); o.addProperty("uy", u.y); o.addProperty("uz", u.z);
            o.addProperty("face", p.normal.getName());
            a.add(o);
        }
        j.add("portals", a); j.addProperty("linked", s[0] != null && s[1] != null); j.addProperty("entityTeleports", ENTITY_TELEPORTS);
        j.addProperty("objects", testObjects.size());
        return j;
    }
}
