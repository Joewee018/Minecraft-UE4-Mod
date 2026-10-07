package crb.client.sm64;

import com.google.gson.JsonObject;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.server.IntegratedServer;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

import java.util.EnumMap;
import java.util.Map;
import java.util.UUID;

/**
 * SM64 Steve Movement: a Super Mario 64 style movement state machine for the local player, written for this project.
 * The SM64 decompilation was used only as a behaviour reference (speeds, timings, which input leads where); none of its
 * code is copied.
 *
 * Runs on the client thread from Player.travel (crb.client.mixin.PlayerTravelMixin) instead of vanilla's travel, so
 * the result is ordinary client-authoritative player movement: Entity.move does the block collision and step-up,
 * LocalPlayer sends the positions, and the integrated server validates them exactly as for vanilla walking.
 * The physics run at SM64's 30 Hz (1-2 frames per 20 Hz Minecraft tick). It hands control back to vanilla in water,
 * lava, on ladders, while flying, riding, gliding, sleeping or dead, and whenever the Unreal host stops asking for it.
 */
public final class Sm64Controller {
    public static final Sm64Controller INSTANCE = new Sm64Controller();

    public enum Action {
        IDLE, WALK, SKID, BRAKE, CROUCH, CROUCH_SLIDE, LAND, HARD_LAND,
        JUMP, DOUBLE_JUMP, TRIPLE_JUMP, BACKFLIP, SIDE_FLIP, LONG_JUMP, FREEFALL,
        GROUND_POUND, GROUND_POUND_LAND, AIR_HIT_WALL, WALL_KICK, BONK;

        public boolean airborne() {
            return switch (this) {
                case JUMP, DOUBLE_JUMP, TRIPLE_JUMP, BACKFLIP, SIDE_FLIP, LONG_JUMP, FREEFALL, GROUND_POUND, AIR_HIT_WALL, WALL_KICK, BONK -> true;
                default -> false;
            };
        }
    }

    // ---- host input (HostInput, client thread) ----
    private int requestTicks;                 // >0 while the host's input asks for SM64 (latched for 1 s)
    private float stickFwd, stickStrafe, camYaw;
    private boolean jumpHeld, crouchHeld, walkHeld;
    private long jumpPresses = -1, crouchPresses = -1;
    private boolean jumpQueued, crouchQueued;

    // ---- movement state ----
    private boolean active;
    private Action action = Action.IDLE;
    private int timer;
    private double fwdVel, sideVel, velY;     // SM64 units per frame
    private float faceYaw;                    // Minecraft yaw, degrees
    private boolean grounded, cutJump;
    private int chainStep, chainTimer;
    private double frameAcc, fallStartY, takeoffY, apexY, wallHitSpeed;
    private double wallNx, wallNz;
    private Vec3 lastSet = Vec3.ZERO, lastPos = Vec3.ZERO;
    private int teleports;
    private double intendedMag; private float intendedYaw;
    private boolean serverFlag;

    // ---- evidence for the Unreal HUD / tests ----
    private int jumpSerial, landSerial, actionSerial, wallKicks, groundPounds, knockbacks, enters, exits;
    private long frames;
    private double maxFwdVel, lastJumpHeight, lastLaunchVel;
    private String lastExitReason = "", lastEligibility = "";
    private final Map<Action, Integer> counts = new EnumMap<>(Action.class);

    public boolean active() { return active; }
    public boolean requested() { return requestTicks > 0; }

    /** Called by HostInput every client tick with fresh host input. */
    public void input(boolean sm64, float fwd, float strafe, float yaw, boolean jump, boolean crouch, boolean walk, long jPresses, long cPresses) {
        if (!sm64) { requestTicks = Math.max(0, requestTicks - 20); jumpPresses = crouchPresses = -1; return; }
        requestTicks = 20;
        stickFwd = fwd; stickStrafe = strafe; camYaw = yaw;
        if (jump && !jumpHeld) jumpQueued = true;
        if (crouch && !crouchHeld) crouchQueued = true;
        if (jumpPresses >= 0 && jPresses > jumpPresses) jumpQueued = true;
        if (crouchPresses >= 0 && cPresses > crouchPresses) crouchQueued = true;
        jumpPresses = jPresses; crouchPresses = cPresses;
        jumpHeld = jump; crouchHeld = crouch; walkHeld = walk;
    }

    /** Host input went stale: keep simulating with a neutral stick for the latch period. */
    public void stale() {
        if (requestTicks > 0) requestTicks--;
        stickFwd = stickStrafe = 0; jumpHeld = crouchHeld = walkHeld = false;
    }

    /** Whether SM64 should drive this player now (HostInput uses this to stop pressing vanilla movement keys). */
    public boolean wants(LocalPlayer p) { return requestTicks > 0 && eligible(p); }

    private boolean eligible(LocalPlayer p) {
        String why = !p.isAlive() ? "dead" : p.isSpectator() ? "spectator" : p.getAbilities().flying ? "flying" : p.isPassenger() ? "riding"
            : p.isInWater() ? "water" : p.isInLava() ? "lava" : p.onClimbable() ? "ladder" : p.isFallFlying() ? "elytra" : p.isSleeping() ? "sleeping" : "";
        lastEligibility = why;
        return why.isEmpty();
    }

    /** Player.travel hook. Returns true when SM64 moved the player this tick (vanilla travel is then skipped). */
    public boolean travel(Player player) {
        if (!(player instanceof LocalPlayer p) || p != Minecraft.getInstance().player) return false;
        if (requestTicks <= 0 || !eligible(p)) {
            if (active) exit(p, requestTicks <= 0 ? "disabled" : lastEligibility);
            return false;
        }
        if (!active) enter(p);
        tick(p);
        return true;
    }

    /** Full stop (world change / disconnect). */
    public void reset(LocalPlayer p) {
        if (active && p != null) exit(p, "reset");
        active = false; requestTicks = 0;
    }

    // =====================================================================================================
    private void enter(LocalPlayer p) {
        active = true; enters++;
        faceYaw = p.getYRot();
        Vec3 dm = p.getDeltaMovement();
        double perFrame = 1.5 * Sm64Config.UNIT;
        Vec3 f = dir(faceYaw);
        fwdVel = (dm.x * f.x + dm.z * f.z) / perFrame / Sm64Config.speedMultiplier;
        sideVel = 0;
        velY = dm.y / perFrame;
        grounded = p.onGround();
        frameAcc = 0; chainStep = 0; chainTimer = 0; cutJump = false;
        fallStartY = apexY = takeoffY = p.getY();
        lastSet = dm; lastPos = p.position();
        set(grounded ? (Math.abs(fwdVel) > 1 ? Action.WALK : Action.IDLE) : Action.FREEFALL);
        if (grounded && fwdVel < 0) fwdVel = 0;
        syncServerFlag(p);
    }

    private void exit(LocalPlayer p, String reason) {
        active = false; exits++; lastExitReason = reason;
        // Hand the current velocity to vanilla so leaving mid-air (water, ladder, toggle) is continuous.
        p.setDeltaMovement(velocityPerTick());
        syncServerFlag(p);
    }

    private void syncServerFlag(LocalPlayer p) {
        boolean want = active && !Sm64Config.vanillaFallDamage;
        if (want == serverFlag) return;
        serverFlag = want;
        IntegratedServer server = Minecraft.getInstance().getSingleplayerServer();
        UUID id = p.getUUID();
        if (server != null) server.execute(() -> crb.Sm64Server.set(id, want));
    }

    /** Settings changed: the fall-damage flag may need to follow. */
    public void configChanged() { LocalPlayer p = Minecraft.getInstance().player; if (p != null) syncServerFlag(p); }

    private void tick(LocalPlayer p) {
        // A teleport (server correction, /tp, respawn) restarts the state machine at the new place.
        Vec3 dm = p.getDeltaMovement();
        if (p.position().distanceToSqr(lastPos) > 4.0) {
            teleports++;
            fwdVel = sideVel = velY = 0; lastSet = dm; faceYaw = p.getYRot();
            set(p.onGround() ? Action.IDLE : Action.FREEFALL); leaveGround(p);
        }
        // External impulses (mob hits, explosions) arrive as a replaced deltaMovement: take them as a knockback.
        if (dm.subtract(lastSet).horizontalDistance() > 0.12 || dm.y - lastSet.y > 0.2) {
            double perFrame = 1.5 * Sm64Config.UNIT;
            double h = dm.horizontalDistance() / perFrame;
            if (h > 2) faceYaw = yawOf(-dm.x, -dm.z);              // face back toward the source
            fwdVel = -h / Sm64Config.speedMultiplier;
            velY = Math.max(velY, dm.y / perFrame);
            knockbacks++;
            set(Action.BONK); leaveGround(p);
        }
        // Stick -> intended speed (0..32) and world yaw relative to the camera.
        double stick = Math.min(1.0, Math.sqrt(stickFwd * stickFwd + stickStrafe * stickStrafe));
        if (stick < 0.08) stick = 0;
        if (Sm64Config.ctrlWalks && walkHeld) stick = Math.min(stick, Sm64Config.walkStick);
        intendedMag = stick * Sm64Config.maxIntendedSpeed;
        intendedYaw = stick > 0 ? camYaw + (float) Math.toDegrees(Math.atan2(stickStrafe, stickFwd)) : faceYaw;

        frameAcc += 1.5;
        int n = (int) frameAcc;
        frameAcc -= n;
        Vec3 start = p.position();
        for (int i = 0; i < n; i++) {
            boolean jp = i == 0 && jumpQueued, cp = i == 0 && crouchQueued;
            if (i == 0) { jumpQueued = false; crouchQueued = false; }
            frame(p, jp, cp);
            frames++;
        }
        Vec3 moved = p.position().subtract(start);
        lastSet = moved; lastPos = p.position();
        p.setDeltaMovement(moved);
        p.setYRot(faceYaw);
        p.setYBodyRot(faceYaw);
        p.setYHeadRot(faceYaw);
        p.setSprinting(false);
        p.resetFallDistance();
        maxFwdVel = Math.max(maxFwdVel, fwdVel);
        if (!active) return;
        syncServerFlag(p);
    }

    // =====================================================================================================
    private void frame(LocalPlayer p, boolean jp, boolean cp) {
        timer++;
        if (!action.airborne() && chainTimer > 0) chainTimer--;
        final double mag = intendedMag;
        final double dYaw = Mth.wrapDegrees(intendedYaw - faceYaw);
        switch (action) {
            case IDLE -> {
                fwdVel = 0; sideVel = 0;
                if (jp) { groundJump(p); return; }
                if (crouchHeld) { set(Action.CROUCH); }
                else if (mag > 0) { faceYaw = intendedYaw; set(Action.WALK); }
                groundStep(p);
            }
            case WALK -> {
                if (jp) { groundJump(p); return; }
                if (cp) { set(fwdVel >= 8 ? Action.CROUCH_SLIDE : Action.CROUCH); groundStep(p); return; }
                if (mag == 0) {
                    if (fwdVel >= Sm64Config.skidMinSpeed) set(Action.BRAKE);
                    else { fwdVel = approach(fwdVel, 0, Sm64Config.coastDecel); if (fwdVel <= 0) set(Action.IDLE); }
                } else if (fwdVel >= Sm64Config.skidMinSpeed && Math.abs(dYaw) > Sm64Config.turnAroundAngle) {
                    set(Action.SKID);
                } else {
                    accelerate(mag);
                    faceYaw = approachAngle(faceYaw, intendedYaw, (float) Sm64Config.turnRate);
                }
                groundStep(p);
            }
            case BRAKE -> {
                if (jp) { groundJump(p); return; }
                fwdVel = approach(fwdVel, 0, Sm64Config.brakeDecel);
                if (mag > 0 && Math.abs(dYaw) <= 90) set(Action.WALK);
                else if (fwdVel <= 0) set(Action.IDLE);
                groundStep(p);
            }
            case SKID -> {
                if (jp) { launch(Action.SIDE_FLIP, p); return; }
                fwdVel = approach(fwdVel, 0, Sm64Config.skidDecel);
                if (fwdVel <= 0) {
                    if (mag > 0) { faceYaw = intendedYaw; fwdVel = 0; set(Action.WALK); } else set(Action.IDLE);
                }
                groundStep(p);
            }
            case CROUCH -> {
                fwdVel = 0;
                if (jp) { launch(Action.BACKFLIP, p); return; }
                if (!crouchHeld) set(Action.IDLE);
                groundStep(p);
            }
            case CROUCH_SLIDE -> {
                if (jp) {
                    if (Sm64Config.longJump && timer <= Sm64Config.longJumpWindowFrames && fwdVel > Sm64Config.longJumpMinSpeed) { launch(Action.LONG_JUMP, p); return; }
                    if (timer >= Sm64Config.crouchSlideJumpFrames) { launch(Action.JUMP, p); return; }
                }
                fwdVel *= 0.95;
                if (mag > 0) faceYaw = approachAngle(faceYaw, intendedYaw, (float) Sm64Config.airTurn);
                if (fwdVel < 2) { fwdVel = 0; set(crouchHeld ? Action.CROUCH : Action.IDLE); }
                groundStep(p);
            }
            case LAND -> {
                if (jp) { groundJump(p); return; }
                if (mag > 0 && timer > 1) { set(Action.WALK); }
                else {
                    fwdVel = approach(fwdVel, 0, Sm64Config.brakeDecel);
                    if (timer >= Sm64Config.landFrames) set(fwdVel > 0 ? Action.WALK : Action.IDLE);
                }
                groundStep(p);
            }
            case HARD_LAND, GROUND_POUND_LAND -> {
                fwdVel = approach(fwdVel, 0, 4);
                int len = action == Action.HARD_LAND ? Sm64Config.hardLandFrames : Sm64Config.groundPoundLandFrames;
                if (timer >= len) set(Action.IDLE);
                groundStep(p);
            }
            case JUMP, DOUBLE_JUMP, FREEFALL, WALL_KICK -> air(p, true, cp, jp);
            case TRIPLE_JUMP, BACKFLIP, SIDE_FLIP, LONG_JUMP -> air(p, false, cp, jp);
            case BONK -> { fwdVel = approach(fwdVel, 0, 0.35); sideVel = 0; gravity(); airStep(p); afterAir(p, false, jp, false); }
            case AIR_HIT_WALL -> {
                // Stuck to the wall for the kick window: A kicks off, otherwise bonk back off it.
                fwdVel = 0; sideVel = 0;
                if (jp && Sm64Config.wallKicks) { wallKick(p); return; }
                gravity(); airStep(p);
                if (grounded) { land(p); return; }
                if (timer >= Sm64Config.wallKickWindowFrames) {
                    fwdVel = wallHitSpeed >= Sm64Config.hardBonkSpeed ? -16 : -8;
                    set(Action.BONK);
                }
            }
            case GROUND_POUND -> {
                fwdVel = 0; sideVel = 0;
                if (timer <= Sm64Config.groundPoundSpinFrames) {
                    velY = timer <= 4 ? 4 : 0;                 // small hop while spinning (SM64's rise before the drop)
                } else {
                    velY = -Sm64Config.groundPoundVel * Sm64Config.gravityMultiplier;
                }
                airStep(p);
                if (grounded && velY <= 0) land(p);
            }
        }
    }

    // ---- ground ----
    private void accelerate(double target) {
        if (fwdVel <= 0) fwdVel += 1.1;
        else if (fwdVel <= target) fwdVel += 1.1 - fwdVel / 43.0;
        else fwdVel -= 1.0;
        fwdVel = Math.min(fwdVel, Sm64Config.groundCap);
    }

    private void groundJump(LocalPlayer p) {
        if (chainTimer > 0 && chainStep == 2 && fwdVel > Sm64Config.tripleMinSpeed) launch(Action.TRIPLE_JUMP, p);
        else if (chainTimer > 0 && chainStep == 1) launch(Action.DOUBLE_JUMP, p);
        else launch(Action.JUMP, p);
    }

    private void launch(Action a, LocalPlayer p) {
        double jm = Sm64Config.jumpMultiplier;
        switch (a) {
            case JUMP -> { velY = (Sm64Config.jumpVel + fwdVel * Sm64Config.jumpSpeedBonus) * jm; fwdVel *= Sm64Config.jumpCarry; }
            case DOUBLE_JUMP -> { velY = (Sm64Config.doubleJumpVel + fwdVel * Sm64Config.jumpSpeedBonus) * jm; fwdVel *= Sm64Config.jumpCarry; }
            case TRIPLE_JUMP -> { velY = Sm64Config.tripleJumpVel * jm; fwdVel *= Sm64Config.jumpCarry; }
            case BACKFLIP -> { velY = Sm64Config.backflipVel * jm; fwdVel = -16; }
            case SIDE_FLIP -> { velY = Sm64Config.sideflipVel * jm; fwdVel = 8; if (intendedMag > 0) faceYaw = intendedYaw; }
            case LONG_JUMP -> { velY = Sm64Config.longJumpVel * jm; fwdVel = Math.min(fwdVel * Sm64Config.longJumpMult, Sm64Config.longJumpCap); }
            default -> { }
        }
        sideVel = 0; cutJump = false; chainTimer = 0;
        lastLaunchVel = velY;
        jumpSerial++;
        set(a);
        leaveGround(p);
        airStep(p);           // the take-off frame moves already
        afterAir(p, a == Action.JUMP || a == Action.DOUBLE_JUMP, false, false);
    }

    private void wallKick(LocalPlayer p) {
        // Reflect the facing off the wall: head-on kicks straight back.
        Vec3 d = dir(faceYaw);
        double dot = d.x * wallNx + d.z * wallNz;
        double rx = d.x - 2 * dot * wallNx, rz = d.z - 2 * dot * wallNz;
        faceYaw = yawOf(rx, rz);
        fwdVel = Math.max(24, Math.min(wallHitSpeed, 32));
        velY = Sm64Config.wallKickVel * Sm64Config.jumpMultiplier;
        sideVel = 0; cutJump = false;
        wallKicks++; jumpSerial++;
        lastLaunchVel = velY;
        set(Action.WALL_KICK);
        leaveGround(p);
        airStep(p);
    }

    private void leaveGround(LocalPlayer p) { grounded = false; takeoffY = fallStartY = apexY = p.getY(); }

    /** Horizontal move, then glue to a floor within snapDownBlocks (stairs / slabs going down), else start falling. */
    private void groundStep(LocalPlayer p) {
        velY = 0;
        Vec3 h = horizontal();
        Vec3 before = p.position();
        p.move(MoverType.SELF, new Vec3(h.x, 0, h.z));
        wallCheck(p, before, h);
        AABB box = p.getBoundingBox();
        double snap = Sm64Config.snapDownBlocks;
        if (!p.level().noCollision(p, box.move(0, -snap, 0))) {
            p.move(MoverType.SELF, new Vec3(0, -snap, 0));
            grounded = true;
        } else {
            grounded = false;
        }
        if (!grounded) {
            leaveGround(p);
            set(Action.FREEFALL);
            return;
        }
        if (p.horizontalCollision && action == Action.WALK && headOn()) fwdVel = Math.min(fwdVel, 6); // pushing on a wall
    }

    // ---- air ----
    private void gravity() {
        double g = (action == Action.LONG_JUMP ? Sm64Config.longJumpGravity : Sm64Config.gravity) * Sm64Config.gravityMultiplier;
        velY = Math.max(velY - g, -Sm64Config.terminal * Sm64Config.gravityMultiplier);
    }

    private void air(LocalPlayer p, boolean turn, boolean cp, boolean jp) {
        if (cp && Sm64Config.groundPound) { groundPounds++; set(Action.GROUND_POUND); velY = 0; return; }
        boolean controlHeight = action == Action.JUMP || action == Action.DOUBLE_JUMP;
        if (controlHeight && !cutJump && !jumpHeld && velY > 20) { velY /= 4; cutJump = true; }
        gravity();
        double m = intendedMag / 32.0;
        double dYaw = Math.toRadians(Mth.wrapDegrees(intendedYaw - faceYaw));
        fwdVel = approach(fwdVel, 0, Sm64Config.airDrag);
        if (m > 0) {
            fwdVel += Sm64Config.airAccel * Math.cos(dYaw) * m;
            if (turn) { faceYaw += (float) (Sm64Config.airTurn * Math.sin(dYaw) * m); sideVel = 0; }
            else sideVel = Sm64Config.sideAirSpeed * Math.sin(dYaw) * m;
        } else sideVel = 0;
        double drag = action == Action.LONG_JUMP ? Sm64Config.longJumpCap : Sm64Config.airMaxFwd;
        if (fwdVel > drag) fwdVel -= 1;
        if (fwdVel < Sm64Config.airMaxBack) fwdVel += 2;
        airStep(p);
        afterAir(p, controlHeight, jp, true);
    }

    private void afterAir(LocalPlayer p, boolean controlHeight, boolean jp, boolean canHitWall) {
        if (!active) return;
        if (grounded && velY <= 0) { land(p); return; }
        if (canHitWall && p.horizontalCollision && fwdVel > Sm64Config.wallHitMinSpeed && headOn()
            && action != Action.BACKFLIP && action != Action.GROUND_POUND) {
            wallHitSpeed = fwdVel;
            if (Sm64Config.wallKicks) set(Action.AIR_HIT_WALL);
            else { fwdVel = fwdVel >= Sm64Config.hardBonkSpeed ? -16 : -8; set(Action.BONK); }
        }
    }

    private void airStep(LocalPlayer p) {
        Vec3 h = horizontal();
        double dy = velY * Sm64Config.UNIT;
        Vec3 before = p.position();
        p.move(MoverType.SELF, new Vec3(h.x, dy, h.z));
        wallCheck(p, before, h);
        if (p.verticalCollision && !p.onGround() && velY > 0) velY = 0;   // head bonk on a ceiling
        grounded = p.onGround() && velY <= 0;
        apexY = Math.max(apexY, p.getY());
        lastJumpHeight = Math.max(0, apexY - takeoffY);
    }

    private void land(LocalPlayer p) {
        double fell = apexY - p.getY();
        Action from = action;
        velY = 0; grounded = true; landSerial++;
        chainStep = from == Action.JUMP ? 1 : from == Action.DOUBLE_JUMP ? 2 : 0;
        chainTimer = chainStep > 0 ? Sm64Config.chainWindowFrames : 0;
        sideVel = 0;
        if (from == Action.GROUND_POUND) { fwdVel = 0; set(Action.GROUND_POUND_LAND); }
        else if ((from == Action.FREEFALL && fell > Sm64Config.hardLandBlocks) || (from == Action.BONK && fwdVel <= -12)) { set(Action.HARD_LAND); }
        else {
            if (from == Action.BACKFLIP || from == Action.BONK) fwdVel = 0;
            set(Action.LAND);
        }
        fallStartY = apexY = p.getY();
    }

    // ---- helpers ----
    private void wallCheck(LocalPlayer p, Vec3 before, Vec3 want) {
        if (!p.horizontalCollision) return;
        Vec3 got = p.position().subtract(before);
        double nx = 0, nz = 0;
        if (Math.abs(got.x - want.x) > 1e-4) nx = -Math.signum(want.x);
        if (Math.abs(got.z - want.z) > 1e-4) nz = -Math.signum(want.z);
        double l = Math.sqrt(nx * nx + nz * nz);
        if (l > 0) { wallNx = nx / l; wallNz = nz / l; }
    }

    private boolean headOn() {
        Vec3 d = dir(faceYaw);
        return d.x * wallNx + d.z * wallNz < -0.5;   // facing within 60 degrees of straight into the wall
    }

    private Vec3 horizontal() {
        double s = Sm64Config.UNIT * Sm64Config.speedMultiplier;
        Vec3 f = dir(faceYaw), r = dir(faceYaw + 90f);
        return new Vec3((f.x * fwdVel + r.x * sideVel) * s, 0, (f.z * fwdVel + r.z * sideVel) * s);
    }

    private Vec3 velocityPerTick() {
        Vec3 h = horizontal();
        return new Vec3(h.x * 1.5, velY * Sm64Config.UNIT * 1.5, h.z * 1.5);
    }

    private void set(Action a) {
        if (a != action) { action = a; actionSerial++; counts.merge(a, 1, Integer::sum); }
        timer = 0;
    }

    static Vec3 dir(float yaw) {
        double r = Math.toRadians(yaw);
        return new Vec3(-Math.sin(r), 0, Math.cos(r));
    }

    static float yawOf(double x, double z) { return (float) Math.toDegrees(Math.atan2(-x, z)); }

    static double approach(double v, double target, double step) {
        return v < target ? Math.min(target, v + step) : Math.max(target, v - step);
    }

    static float approachAngle(float cur, float target, float step) {
        float d = Mth.wrapDegrees(target - cur);
        return Math.abs(d) <= step ? target : cur + Math.signum(d) * step;
    }

    // =====================================================================================================
    /** State for Unreal (animation + HUD) and the test suite. */
    public JsonObject export(LocalPlayer p) {
        JsonObject j = new JsonObject();
        j.addProperty("requested", requestTicks > 0);
        j.addProperty("active", active);
        j.addProperty("action", action.name());
        j.addProperty("actionId", action.ordinal());
        j.addProperty("timer", timer);
        j.addProperty("fwdVel", round(fwdVel)); j.addProperty("sideVel", round(sideVel)); j.addProperty("velY", round(velY));
        j.addProperty("faceYaw", round(Mth.wrapDegrees(faceYaw)));
        j.addProperty("intendedMag", round(intendedMag)); j.addProperty("intendedYaw", round(Mth.wrapDegrees(intendedYaw)));
        j.addProperty("grounded", grounded);
        j.addProperty("chain", chainStep); j.addProperty("chainTimer", chainTimer);
        j.addProperty("jumpSerial", jumpSerial); j.addProperty("landSerial", landSerial); j.addProperty("actionSerial", actionSerial);
        j.addProperty("wallKicks", wallKicks); j.addProperty("groundPounds", groundPounds); j.addProperty("knockbacks", knockbacks); j.addProperty("teleports", teleports);
        j.addProperty("frames", frames); j.addProperty("enters", enters); j.addProperty("exits", exits);
        j.addProperty("maxFwdVel", round(maxFwdVel)); j.addProperty("lastJumpHeight", round(lastJumpHeight)); j.addProperty("lastLaunchVel", round(lastLaunchVel));
        j.addProperty("exitReason", lastExitReason); j.addProperty("blocked", lastEligibility);
        j.addProperty("serverNoFall", serverFlag);
        JsonObject c = new JsonObject();
        for (Map.Entry<Action, Integer> e : counts.entrySet()) c.addProperty(e.getKey().name(), e.getValue());
        j.add("counts", c);
        if (p != null) {
            j.addProperty("skin", p.getSkinTextureLocation().toString());
            j.addProperty("slim", "slim".equals(p.getModelName()));
        }
        return j;
    }

    /** Test support: clear the evidence counters. */
    public void resetStats() {
        counts.clear(); wallKicks = groundPounds = knockbacks = 0; maxFwdVel = 0; lastJumpHeight = 0; lastLaunchVel = 0;
    }

    private static double round(double v) { return Math.round(v * 1000.0) / 1000.0; }
}
