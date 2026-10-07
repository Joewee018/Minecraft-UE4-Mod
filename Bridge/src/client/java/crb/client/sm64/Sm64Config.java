package crb.client.sm64;

import com.google.gson.JsonObject;
import net.minecraft.util.Mth;

/**
 * SM64 Steve Movement settings. Unreal owns persistence (Config ini) and pushes these with the "sm64.config" op on
 * connect and whenever a setting changes. Distances in SM64 units (1 unit = UNIT blocks), speeds in units per 30 Hz
 * frame, angles in degrees. The defaults reproduce the original game's feel at Steve's scale.
 */
public final class Sm64Config {
    private Sm64Config() { }

    /** Mario is ~160 units tall; Steve is 1.8 blocks -> 1 unit = 0.01125 blocks. */
    public static final double UNIT = 0.01125;

    // ---- user settings (Mod Menu -> SM64 Steve Movement settings) ----
    public static double speedMultiplier = 1.0;   // horizontal speeds
    public static double jumpMultiplier = 1.0;    // every jump's launch speed
    public static double gravityMultiplier = 1.0;
    public static boolean wallKicks = true;
    public static boolean groundPound = true;
    public static boolean longJump = true;
    public static boolean vanillaFallDamage = false; // SM64 has no fall damage at these heights
    public static boolean ctrlWalks = true;          // keyboard: Ctrl held = walk (half stick), else full stick = run

    // ---- physics (reimplemented from observed SM64 behaviour; see Docs/SM64.md) ----
    public static double maxIntendedSpeed = 32;      // full stick target speed
    public static double walkStick = 0.375;          // stick magnitude used for "walk" (12 units/frame)
    public static double groundCap = 48;
    public static double turnRate = 11.25;           // degrees per frame while walking (0x800)
    public static double turnAroundAngle = 100;      // stick vs facing beyond this at speed -> skid turn-around (0x471C)
    public static double skidMinSpeed = 16;
    public static double brakeDecel = 2, coastDecel = 1, skidDecel = 2;
    public static double gravity = 4, terminal = 75, longJumpGravity = 2;
    public static double jumpVel = 42, doubleJumpVel = 52, tripleJumpVel = 69, backflipVel = 62, sideflipVel = 62;
    public static double longJumpVel = 30, wallKickVel = 62, groundPoundVel = 50;
    public static double jumpSpeedBonus = 0.25;      // launch += fwdVel * this (single/double)
    public static double jumpCarry = 0.8;            // fwdVel kept at take-off
    public static double tripleMinSpeed = 20;
    public static double longJumpMinSpeed = 10, longJumpMult = 1.5, longJumpCap = 48;
    public static double airDrag = 0.35, airAccel = 1.5, airTurn = 2.8125, airMaxFwd = 32, airMaxBack = -16;
    public static double sideAirSpeed = 10;          // sideways air control of flips (no turning)
    public static int chainWindowFrames = 6;         // landing -> next jump of the triple-jump chain
    public static int wallKickWindowFrames = 5;
    public static double wallHitMinSpeed = 16, hardBonkSpeed = 38;
    public static int landFrames = 4, hardLandFrames = 15, groundPoundLandFrames = 18, groundPoundSpinFrames = 10;
    public static double hardLandBlocks = 7.0;       // falls longer than this play the hard landing
    public static int crouchSlideJumpFrames = 5, longJumpWindowFrames = 30;
    public static double crouchSlideFriction = 0.92;
    public static double snapDownBlocks = 0.55;       // stay glued to stairs/slabs going down

    public static void apply(JsonObject j) {
        speedMultiplier = d(j, "speedMultiplier", speedMultiplier, 0.25, 3);
        jumpMultiplier = d(j, "jumpMultiplier", jumpMultiplier, 0.25, 3);
        gravityMultiplier = d(j, "gravityMultiplier", gravityMultiplier, 0.25, 3);
        wallKicks = b(j, "wallKicks", wallKicks);
        groundPound = b(j, "groundPound", groundPound);
        longJump = b(j, "longJump", longJump);
        vanillaFallDamage = b(j, "vanillaFallDamage", vanillaFallDamage);
        ctrlWalks = b(j, "ctrlWalks", ctrlWalks);
        chainWindowFrames = (int) d(j, "chainWindowFrames", chainWindowFrames, 1, 30);
        wallKickWindowFrames = (int) d(j, "wallKickWindowFrames", wallKickWindowFrames, 1, 30);
    }

    public static JsonObject toJson() {
        JsonObject j = new JsonObject();
        j.addProperty("speedMultiplier", speedMultiplier);
        j.addProperty("jumpMultiplier", jumpMultiplier);
        j.addProperty("gravityMultiplier", gravityMultiplier);
        j.addProperty("wallKicks", wallKicks);
        j.addProperty("groundPound", groundPound);
        j.addProperty("longJump", longJump);
        j.addProperty("vanillaFallDamage", vanillaFallDamage);
        j.addProperty("ctrlWalks", ctrlWalks);
        j.addProperty("chainWindowFrames", chainWindowFrames);
        j.addProperty("wallKickWindowFrames", wallKickWindowFrames);
        return j;
    }

    private static double d(JsonObject j, String k, double cur, double lo, double hi) {
        if (!j.has(k)) return cur;
        double v = j.get(k).getAsDouble();
        return Double.isFinite(v) ? Mth.clamp(v, lo, hi) : cur;
    }

    private static boolean b(JsonObject j, String k, boolean cur) { return j.has(k) ? j.get(k).getAsBoolean() : cur; }
}
