package crb.client.pp;

import com.google.gson.JsonObject;
import crb.pp.Portals;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.server.IntegratedServer;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.VoxelShape;

import java.util.EnumMap;
import java.util.Map;
import java.util.UUID;

/**
 * Physics & Portal mod - the local player's physics controller (client thread, from Player.travel).
 * Neverball-style momentum: a real velocity vector driven by acceleration, slope gravity projected on the ground plane,
 * surface friction / drag / bounce from the data-driven surface table (PPSurfaces), Gish-style modifiers (PPConfig),
 * launches from ramps (the ground-plane velocity keeps its upward component when the ground ends), air control,
 * sliding on low-friction ground, rolling (hold the roll key: a ball with low rolling friction and full slope pull),
 * physics landings with ragdoll on hard impacts, and portal traversal with the velocity / facing transformed by
 * crb.pp.Portals. Collision is vanilla Entity.move against the real Minecraft blocks; Minecraft keeps every other
 * system (blocks, items, inventory, redstone, mobs, damage). Water, ladders, flying, riding and spectating hand the
 * player back to vanilla movement for that moment.
 */
public final class PPController {
    public static final PPController INSTANCE = new PPController();
    static final double DT = 1.0 / 60.0, HALF_W = 0.3, HALF_H = 0.9, BALL_R = 0.45;

    public enum State { IDLE, WALK, RUN, JUMP, FALL, LAND, ROLL, SLIDE, LAUNCH, PORTAL_ENTER, PORTAL_EXIT, HIT, RAGDOLL }

    // input
    private int requestTicks; private float fwd, strafe, camYaw; private boolean jumpHeld, sprintHeld, rollHeld, jumpPressed;
    private long jumpPresses = -1, portalA = -1, portalB = -1, portalClear = -1;
    // simulation
    private boolean active, serverFlag, grounded, wasGrounded, controllerOn = true, forceRoll, forceSlide;
    private Vec3 vel = Vec3.ZERO, normal = new Vec3(0, 1, 0), lastPos = Vec3.ZERO;
    private State state = State.IDLE; private double stateTime, ragdollTime, timer, portalCd, heading, bodyYaw;
    private PPSurfaces.Surface surface = PPSurfaces.DEFAULT; private String surfaceBlock = "";
    private long frames, landSeq, bounceSeq, portalSeq, ragdollSeq, launchSeq; private double lastImpact, yawDelta, maxSpeed;
    private String lastPortal = "", lastEvent = "";
    private final Map<State, Integer> counts = new EnumMap<>(State.class);
    private Vec3 ragdollImpulse = Vec3.ZERO;

    public boolean active() { return active; }
    public boolean requested() { return requestTicks > 0; }

    // ================================================================================================= input
    public void input(boolean on, float fwd, float strafe, float yaw, boolean jump, boolean sprint, JsonObject k) {
        if (!on) { requestTicks = Math.max(0, requestTicks - 20); return; }
        requestTicks = 20;
        this.fwd = fwd; this.strafe = strafe; camYaw = yaw; sprintHeld = sprint;
        if (jump && !jumpHeld) jumpPressed = true;
        jumpHeld = jump;
        long jp = k != null && k.has("jumpPresses") ? k.get("jumpPresses").getAsLong() : -1;
        if (jumpPresses >= 0 && jp > jumpPresses) jumpPressed = true;
        jumpPresses = jp;
        rollHeld = k != null && k.has("roll") && k.get("roll").getAsBoolean();
        long a = num(k, "portalA"), b = num(k, "portalB"), c = num(k, "portalClear");
        if (portalA >= 0 && a > portalA) shoot(0);
        if (portalB >= 0 && b > portalB) shoot(1);
        if (portalClear >= 0 && c > portalClear) server(s -> crb.pp.Portals.INSTANCE.clear());
        portalA = a; portalB = b; portalClear = c;
    }
    private static long num(JsonObject k, String f) { return k != null && k.has(f) ? k.get(f).getAsLong() : -1; }

    public void stale() { if (requestTicks > 0) requestTicks--; fwd = strafe = 0; jumpHeld = sprintHeld = rollHeld = false; }

    public boolean wants(LocalPlayer p) { return requestTicks > 0 && controllerOn && eligible(p); }

    private boolean eligible(LocalPlayer p) {
        return p.isAlive() && !p.isSpectator() && !p.getAbilities().flying && !p.isPassenger() && !p.isInWater() && !p.isInLava()
            && !p.onClimbable() && !p.isFallFlying() && !p.isSleeping();
    }

    // ================================================================================================= lifecycle
    /** Each client tick (also while vanilla moves the player): portals on/off follow the mod request. */
    public void clientTick(Minecraft mc) {
        boolean want = requestTicks > 0;
        if (want != Portals.ENABLED) {
            Portals.ENABLED = want;
            if (!want) server(s -> Portals.INSTANCE.cleanup(s));   // OFF: portals and spawned physics objects removed
        }
        if (!want && active && mc.player != null) exit(mc.player);
    }

    public boolean travel(Player player) {
        if (!(player instanceof LocalPlayer p) || p != Minecraft.getInstance().player) return false;
        if (requestTicks <= 0 || !controllerOn || !eligible(p)) { if (active) exit(p); return false; }
        if (!active) enter(p);
        tick(p);
        return true;
    }

    private void enter(LocalPlayer p) {
        active = true;
        Vec3 dm = p.getDeltaMovement();
        vel = new Vec3(dm.x * 20, Math.max(0, dm.y * 20), dm.z * 20);
        heading = bodyYaw = p.getYRot(); lastPos = p.position();
        grounded = p.onGround(); set(grounded ? State.IDLE : State.FALL);
        syncServer(p, true);
        lastEvent = "controller on";
    }

    private void exit(LocalPlayer p) {
        active = false;
        p.setDeltaMovement(vel.scale(DT * 3));
        syncServer(p, false);
        lastEvent = "controller off";
    }

    private void syncServer(LocalPlayer p, boolean on) {
        if (on == serverFlag) return;
        serverFlag = on;
        UUID id = p.getUUID();
        server(s -> crb.Sm64Server.set(id, on));                        // shared "mod drives the player: no fall damage"
    }

    private static void server(java.util.function.Consumer<net.minecraft.server.MinecraftServer> r) {
        IntegratedServer s = Minecraft.getInstance().getSingleplayerServer();
        if (s != null) s.execute(() -> r.accept(s));
    }

    private void shoot(int which) {
        LocalPlayer lp = Minecraft.getInstance().player; if (lp == null) return;
        UUID id = lp.getUUID();
        server(s -> { ServerPlayer sp = s.getPlayerList().getPlayer(id); if (sp != null) lastEvent = Portals.INSTANCE.shoot(sp, which); });
    }

    // ================================================================================================= tick
    private void tick(LocalPlayer p) {
        if (p.position().distanceToSqr(lastPos) > 16 && portalCd <= 0) { vel = Vec3.ZERO; }    // teleported by a command
        int n = PPConfig.frozen ? 0 : 3;
        for (int i = 0; i < n; i++) { step(p, DT * PPConfig.timeScale); frames++; }
        jumpPressed = false;
        p.setDeltaMovement(n == 0 ? Vec3.ZERO : vel.scale(DT * PPConfig.timeScale));
        p.setYRot((float) bodyYaw); p.setYBodyRot((float) bodyYaw); p.setYHeadRot((float) bodyYaw);
        p.setSprinting(false);
        p.resetFallDistance();
        lastPos = p.position();
        maxSpeed = Math.max(maxSpeed, horiz(vel).length());
    }

    void step(LocalPlayer p, double dt) {
        stateTime += dt; timer = Math.max(0, timer - dt); portalCd = Math.max(0, portalCd - dt);
        Level l = p.level();
        PPConfig.Modifier m = PPConfig.modifier;
        double g = PPConfig.v(PPConfig.GRAVITY) * m.gravity;
        // camera-relative wish direction
        double yr = Math.toRadians(camYaw);
        Vec3 f = new Vec3(-Math.sin(yr), 0, Math.cos(yr)), r = new Vec3(-Math.cos(yr), 0, -Math.sin(yr));
        Vec3 wish = f.scale(fwd).add(r.scale(strafe));
        double mag = Math.min(1, wish.length()); Vec3 wdir = mag > 1e-3 ? wish.normalize() : Vec3.ZERO;
        boolean ragdoll = state == State.RAGDOLL;
        if (ragdoll) { ragdollTime -= dt; mag = 0; wdir = Vec3.ZERO; }
        boolean rolling = !ragdoll && (forceRoll || (rollHeld && PPConfig.rolling));

        if (grounded) {
            sampleGround(p, l);
            PPSurfaces.Surface s = surface;
            // slope pull (gravity projected on the ground plane); a ball feels all of it, feet resist most of it
            Vec3 gv = new Vec3(0, -g, 0), gt = gv.subtract(normal.scale(gv.dot(normal)));
            vel = vel.add(gt.scale((rolling || ragdoll ? 1.0 : 0.18) * PPConfig.v(PPConfig.SLOPE) * dt));
            double maxSp = (rolling ? PPConfig.v(PPConfig.ROLL_MAX) : sprintHeld ? PPConfig.v(PPConfig.RUN) : PPConfig.v(PPConfig.WALK)) * s.maxSpeed() * m.maxSpeed;
            double acc = (rolling ? PPConfig.v(PPConfig.ROLL_ACCEL) : PPConfig.v(PPConfig.ACCEL)) * s.accel() * m.accel;
            Vec3 h = horiz(vel);
            if (!PPConfig.momentum && !ragdoll) {
                h = wdir.scale(mag * maxSp);                                        // momentum off: direct control
            } else if (mag > 0) {
                double along = h.dot(wdir);
                if (along < maxSp * mag) h = h.add(wdir.scale(Math.min(acc * mag * dt, maxSp * mag - along)));
                if (!rolling) {                                                     // feet steer: bleed the sideways part
                    Vec3 side = h.subtract(wdir.scale(h.dot(wdir)));
                    h = h.subtract(side.scale(Math.min(1, s.friction() * m.friction * PPConfig.v(PPConfig.FRICTION) * 0.6 * dt)));
                }
            }
            // friction: always for a ball (rolling friction), for feet when not driving or over the speed cap
            double fr = s.friction() * m.friction * PPConfig.v(PPConfig.FRICTION) * (rolling ? 0.12 * s.roll() : 1.0);
            if (rolling || ragdoll || mag < 0.05 || h.length() > maxSp + 0.5 || forceSlide) h = h.scale(Math.max(0, 1 - fr * (forceSlide ? 0.05 : 1) * dt));
            if (s.drag() > 0) h = h.scale(Math.max(0, 1 - s.drag() * h.length() * 0.04 * dt));
            vel = new Vec3(h.x, vel.y, h.z);
            // stay on the ground plane (keeps the slope's vertical part: that is what launches off ramp lips)
            double into = vel.dot(normal);
            if (into < 0) vel = vel.subtract(normal.scale(into));
            if (jumpPressed && !ragdoll) {
                vel = new Vec3(vel.x, Math.max(vel.y, 0) + PPConfig.v(PPConfig.LAUNCH) * (rolling ? 0.8 : 1), vel.z);
                grounded = false; set(State.JUMP); lastEvent = "jump";
                jumpPressed = false;
            }
        } else {
            vel = vel.add(0, -g * dt, 0);
            Vec3 h = horiz(vel);
            if (mag > 0 && !ragdoll) {
                double cap = Math.max(PPConfig.v(PPConfig.WALK), h.length());
                Vec3 nh = h.add(wdir.scale(PPConfig.v(PPConfig.AIR) * m.air * mag * dt));
                if (nh.length() > cap) nh = nh.normalize().scale(cap);
                h = nh;
            }
            vel = new Vec3(h.x, Math.max(vel.y, -60), h.z);
        }

        // ---- move (vanilla collision against the real blocks)
        Vec3 want = vel.scale(dt).add(0, grounded ? -0.02 : 0, 0);
        Vec3 before = p.position();
        Vec3 vIn = vel;                                      // momentum before any collision response (portal surfaces are solid)
        p.move(MoverType.SELF, want);
        Vec3 got = p.position().subtract(before);
        // ---- portals: contact with a portal while moving into it - checked before the wall / floor kills the velocity
        if (portalCd <= 0) {
            Vec3 c = p.position().add(0, HALF_H, 0);
            Portals.Portal in = Portals.entering(c, vIn, HALF_W, HALF_H);
            if (in != null) { vel = vIn; teleport(p, in, c); return; }
        }
        boolean wasAir = !grounded;
        if (p.horizontalCollision && !ragdoll && hop(p, before, want, got)) got = p.position().subtract(before);
        if (p.horizontalCollision) wall(p, want, got, m);
        if (p.verticalCollision) {
            if (want.y < 0) land(p, l, wasAir, m);
            else vel = new Vec3(vel.x, Math.min(0, vel.y), vel.z);
        }
        grounded = p.onGround() && vel.y <= 0.01;
        if (!grounded && p.verticalCollision && want.y < 0 && vel.y > 0) grounded = false;   // bounced off slime


        // ---- state
        if (ragdoll) { if (ragdollTime <= 0 && grounded) { set(State.LAND); timer = 0.35; lastEvent = "recovered"; } }
        else if (timer > 0 && (state == State.LAND || state == State.PORTAL_EXIT || state == State.HIT)) { }
        else if (!grounded) {
            if (rolling) set(State.ROLL);
            else if (state == State.LAUNCH && vel.y > -2) { }
            else set(vel.y > 0 ? (state == State.JUMP ? State.JUMP : State.LAUNCH) : State.FALL);
        } else {
            double sp = horiz(vel).length();
            double fr = surface.friction() * m.friction * PPConfig.v(PPConfig.FRICTION);
            if (rolling) set(State.ROLL);
            else if (forceSlide || (sp > PPConfig.v(PPConfig.WALK) * 1.25 && (fr < 1.5 || mag < 0.05) && sp > 2)) set(State.SLIDE);
            else if (sp > 0.35) set(sp > PPConfig.v(PPConfig.WALK) * 1.15 ? State.RUN : State.WALK);
            else set(State.IDLE);
        }
        // facing: the way we travel (feet turn toward the stick, a ball faces its roll direction)
        Vec3 hv = horiz(vel);
        if (!ragdoll) {
            double target = hv.length() > 0.5 ? Math.toDegrees(Math.atan2(-hv.x, hv.z)) : mag > 0 ? Math.toDegrees(Math.atan2(-wdir.x, wdir.z)) : bodyYaw;
            bodyYaw = bodyYaw + Mth.wrapDegrees(target - bodyYaw) * Math.min(1, dt * (rolling ? 20 : 12));
        }
        heading = hv.length() > 0.1 ? Math.toDegrees(Math.atan2(-hv.x, hv.z)) : heading;
    }

    private void land(LocalPlayer p, Level l, boolean wasAir, PPConfig.Modifier m) {
        double impact = -vel.y;
        BlockPos under = BlockPos.containing(p.getX(), p.getY() - 0.05, p.getZ());
        PPSurfaces.Surface s = PPSurfaces.of(l.getBlockState(under));
        double bounce = Math.max(s.bounce(), m.bounce);
        if (bounce > 0 && impact > 3) {
            vel = new Vec3(vel.x, impact * bounce, vel.z);
            set(State.LAUNCH); bounceSeq++; launchSeq++; lastEvent = "bounce " + String.format("%.1f", impact * bounce) + " m/s on " + s.name();
            return;
        }
        vel = new Vec3(vel.x, 0, vel.z);
        if (wasAir && impact > 2) {
            lastImpact = impact; landSeq++;
            if (PPConfig.ragdollOnImpact && impact > PPConfig.v(PPConfig.RAGDOLL_IMPACT) && m != PPConfig.Modifier.SQUISHY) startRagdoll(vel.add(0, -impact, 0));
            else if (state != State.ROLL) { set(State.LAND); timer = Math.min(0.4, 0.12 + impact * 0.015); lastEvent = "landed " + String.format("%.1f", impact) + " m/s"; }
        }
    }

    /**
     * Momentum hop: a fast body (a rolling ball, or Steve in the air) that meets a ledge up to half a block high rides
     * up onto it instead of stopping dead - vanilla only steps up from the ground. Tried with real collision moves;
     * undone when it gains nothing.
     */
    private boolean hop(LocalPlayer p, Vec3 before, Vec3 want, Vec3 got) {
        Vec3 hw = new Vec3(want.x, 0, want.z);
        double gotH = new Vec3(got.x, 0, got.z).length();
        if (hw.length() < 0.02 || gotH > hw.length() - 1e-4) return false;
        Vec3 after = p.position();
        boolean vc0 = p.verticalCollision, og0 = p.onGround();
        p.setPos(before.x, before.y, before.z);
        p.move(MoverType.SELF, new Vec3(0, 0.55, 0));
        double rose = p.getY() - before.y;
        p.move(MoverType.SELF, hw);
        boolean blocked = p.horizontalCollision;
        p.move(MoverType.SELF, new Vec3(0, -rose + Math.min(0, want.y), 0));
        Vec3 d = p.position().subtract(before);
        if (rose > 0.3 && new Vec3(d.x, 0, d.z).length() > gotH + 0.01 && d.y <= 0.56 && d.y > -1) { p.horizontalCollision = blocked; lastEvent = "hop"; return true; }
        p.setPos(after.x, after.y, after.z);
        p.horizontalCollision = true; p.verticalCollision = vc0; p.setOnGround(og0);
        return false;
    }

    private void wall(LocalPlayer p, Vec3 want, Vec3 got, PPConfig.Modifier m) {
        double nx = Math.abs(got.x - want.x) > 1e-5 ? -Math.signum(want.x) : 0, nz = Math.abs(got.z - want.z) > 1e-5 ? -Math.signum(want.z) : 0;
        Vec3 n = new Vec3(nx, 0, nz); if (n.lengthSqr() < 1e-6) return; n = n.normalize();
        double into = vel.dot(n);
        if (into >= 0) return;
        double rest = m == PPConfig.Modifier.SQUISHY ? 0.75 : surface.bounce() > 0.5 ? surface.bounce() : 0;
        vel = vel.subtract(n.scale(into * (1 + rest)));
        if (m.cling && !grounded && (fwd != 0 || strafe != 0)) { vel = new Vec3(vel.x, Math.max(vel.y, -1.0), vel.z); lastEvent = "sticky wall"; }
        if (-into > PPConfig.v(PPConfig.RAGDOLL_IMPACT) && PPConfig.ragdollOnImpact && m != PPConfig.Modifier.SQUISHY) startRagdoll(n.scale(into));
        else if (-into > 8) { set(State.HIT); timer = 0.3; }
    }

    private void teleport(LocalPlayer p, Portals.Portal a, Vec3 center) {
        Portals.Portal b = Portals.other(a);
        if (b == null) return;
        Vec3 out = Portals.exitCenter(a, b, center, HALF_W, HALF_H);
        Vec3 nv = Portals.mapDir(a, b, vel);
        float oldYaw = (float) bodyYaw, newYaw = Portals.mapYaw(a, b, oldYaw);
        // minimum exit speed so a body always clears the exit portal (Portal: "speedy thing goes in")
        double outSpeed = nv.dot(b.n());
        if (outSpeed < 2) nv = nv.add(b.n().scale(2 - outSpeed));
        p.setPos(out.x, out.y - HALF_H, out.z);
        p.xo = p.getX(); p.yo = p.getY(); p.zo = p.getZ();
        vel = nv;
        yawDelta = Mth.wrapDegrees(newYaw - oldYaw);
        bodyYaw = newYaw; heading = newYaw;
        grounded = false; portalCd = 0.25; portalSeq++;
        lastPortal = (a.id() == 0 ? "A" : "B") + "->" + (b.id() == 0 ? "A" : "B") + " " + String.format("%.1f", nv.length()) + " m/s";
        lastEvent = "portal " + lastPortal;
        set(State.PORTAL_EXIT); timer = 0.35;
        lastPos = p.position();
    }

    private void startRagdoll(Vec3 impulse) {
        ragdollImpulse = impulse; ragdollSeq++; ragdollTime = 2.2;
        set(State.RAGDOLL); lastEvent = "ragdoll";
    }

    /** Ground normal from the collision tops around the feet (4 samples, like a ball touching the surface). */
    private void sampleGround(LocalPlayer p, Level l) {
        double feet = p.getY();
        BlockPos under = BlockPos.containing(p.getX(), feet - 0.05, p.getZ());
        BlockState bs = l.getBlockState(under);
        surface = PPSurfaces.of(bs);
        surfaceBlock = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getKey(bs.getBlock()).toString();
        double hx0 = top(l, p.getX() - 0.45, feet, p.getZ()), hx1 = top(l, p.getX() + 0.45, feet, p.getZ());
        double hz0 = top(l, p.getX(), feet, p.getZ() - 0.45), hz1 = top(l, p.getX(), feet, p.getZ() + 0.45);
        double dx = Mth.clamp((hx1 - hx0) / 0.9, -1.2, 1.2), dz = Mth.clamp((hz1 - hz0) / 0.9, -1.2, 1.2);
        Vec3 nn = new Vec3(-dx, 1, -dz).normalize();
        normal = normal.scale(0.6).add(nn.scale(0.4)).normalize();
    }

    private static double top(Level l, double x, double feet, double z) {
        for (int dy = 0; dy >= -1; dy--) {
            BlockPos bp = BlockPos.containing(x, feet + dy - 0.01, z);
            VoxelShape sh = l.getBlockState(bp).getCollisionShape(l, bp);
            if (!sh.isEmpty()) { double t = bp.getY() + sh.max(net.minecraft.core.Direction.Axis.Y); if (t <= feet + 0.6) return t; }
        }
        return feet - 1;
    }

    private void set(State s) { if (s != state) { state = s; stateTime = 0; counts.merge(s, 1, Integer::sum); } }
    private static Vec3 horiz(Vec3 v) { return new Vec3(v.x, 0, v.z); }

    // ================================================================================================= debug
    public String debug(String cmd, JsonObject a) {
        LocalPlayer p = Minecraft.getInstance().player;
        switch (cmd) {
            case "resetVelocity" -> { vel = Vec3.ZERO; return "velocity reset"; }
            case "resetPlayer" -> { vel = Vec3.ZERO; forceRoll = forceSlide = false; ragdollTime = 0; set(State.IDLE); return "player reset"; }
            case "launch" -> {
                double up = a.has("up") ? a.get("up").getAsDouble() : 14, fw = a.has("forward") ? a.get("forward").getAsDouble() : 6;
                double yr = Math.toRadians(bodyYaw);
                vel = vel.add(-Math.sin(yr) * fw, up, Math.cos(yr) * fw); grounded = false; set(State.LAUNCH); launchSeq++; lastEvent = "debug launch";
                return "launched";
            }
            case "velocity" -> { vel = new Vec3(a.get("x").getAsDouble(), a.get("y").getAsDouble(), a.get("z").getAsDouble()); grounded = false; return "velocity set"; }
            case "forceRoll" -> { forceRoll = a.has("on") ? a.get("on").getAsBoolean() : !forceRoll; return "force roll " + forceRoll; }
            case "forceSlide" -> { forceSlide = a.has("on") ? a.get("on").getAsBoolean() : !forceSlide; return "force slide " + forceSlide; }
            case "ragdoll" -> { if (state == State.RAGDOLL) { ragdollTime = 0; return "ragdoll ending"; } startRagdoll(vel.add(0, 3, 0)); return "ragdoll"; }
            case "controller" -> { controllerOn = a.has("on") ? a.get("on").getAsBoolean() : !controllerOn; if (!controllerOn && p != null && active) exit(p); return "physics controller " + controllerOn; }
            case "teleport" -> {
                Portals.Portal[] s = Portals.SNAPSHOT;
                if (p == null || s[0] == null || s[1] == null) return "need two portals";
                Portals.Portal from = s[a.has("from") ? a.get("from").getAsInt() : 0];
                Vec3 c = from.center().add(from.n().scale(HALF_W + 0.05)).add(0, from.normal().getAxis().isVertical() ? HALF_H : 0, 0);
                teleport(p, from, c);
                return "teleported " + lastPortal;
            }
            case "tune" -> { PPConfig.apply(a.getAsJsonObject("values")); return "tuned"; }
            case "tuneReset" -> { PPConfig.reset(); return "defaults restored"; }
            default -> { return null; }
        }
    }

    public void resetStats() { counts.clear(); landSeq = bounceSeq = portalSeq = ragdollSeq = launchSeq = 0; maxSpeed = 0; lastImpact = 0; }

    // ================================================================================================= export
    public JsonObject export(LocalPlayer p) {
        JsonObject j = new JsonObject();
        j.addProperty("requested", requestTicks > 0); j.addProperty("active", active); j.addProperty("controller", controllerOn);
        j.addProperty("state", state.name()); j.addProperty("stateTime", r(stateTime)); j.addProperty("modifier", PPConfig.modifier.name());
        j.addProperty("grounded", grounded); j.addProperty("rolling", state == State.ROLL);
        j.addProperty("vx", r(vel.x)); j.addProperty("vy", r(vel.y)); j.addProperty("vz", r(vel.z)); j.addProperty("speed", r(horiz(vel).length()));
        j.addProperty("heading", r(Mth.wrapDegrees(heading))); j.addProperty("bodyYaw", r(Mth.wrapDegrees(bodyYaw)));
        j.addProperty("nx", r(normal.x)); j.addProperty("ny", r(normal.y)); j.addProperty("nz", r(normal.z));
        j.addProperty("surface", surface.name()); j.addProperty("surfaceBlock", surfaceBlock); j.addProperty("friction", r(surface.friction() * PPConfig.modifier.friction));
        j.addProperty("bounce", r(Math.max(surface.bounce(), PPConfig.modifier.bounce)));
        j.addProperty("portalSeq", portalSeq); j.addProperty("yawDelta", r(yawDelta)); j.addProperty("lastPortal", lastPortal);
        j.addProperty("ragdoll", state == State.RAGDOLL); j.addProperty("ragdollSeq", ragdollSeq);
        j.addProperty("ix", r(ragdollImpulse.x)); j.addProperty("iy", r(ragdollImpulse.y)); j.addProperty("iz", r(ragdollImpulse.z));
        j.addProperty("landSeq", landSeq); j.addProperty("bounceSeq", bounceSeq); j.addProperty("launchSeq", launchSeq); j.addProperty("impact", r(lastImpact));
        j.addProperty("maxSpeed", r(maxSpeed)); j.addProperty("frames", frames); j.addProperty("event", lastEvent);
        j.addProperty("forceRoll", forceRoll); j.addProperty("forceSlide", forceSlide);
        j.addProperty("timeScale", PPConfig.timeScale); j.addProperty("frozen", PPConfig.frozen); j.addProperty("surfaces", PPSurfaces.count());
        JsonObject c = new JsonObject(); for (Map.Entry<State, Integer> e : counts.entrySet()) c.addProperty(e.getKey().name(), e.getValue()); j.add("counts", c);
        j.add("portals", Portals.INSTANCE.json());
        return j;
    }
    private static double r(double v) { return Math.round(v * 1000.0) / 1000.0; }
}
