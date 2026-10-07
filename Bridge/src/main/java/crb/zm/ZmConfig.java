package crb.zm;

/**
 * Zombies-mode enemy tuning (ticks are server ticks, 20 per second; distances in blocks). One place for every value the
 * AI, combat and animation share. Speeds are vanilla MOVEMENT_SPEED attribute values (vanilla zombie 0.23).
 */
public final class ZmConfig {
    private ZmConfig() { }

    // Perception
    public static double detectRange = 48;          // a player inside this range is a valid target
    public static int loseSightTicks = 60;          // no line of sight this long -> Search the last known position
    public static int searchGiveUpTicks = 200;      // reached the last known position and found nobody -> Idle
    public static int reactionTicks = 6;            // delay between noticing a target and committing to Chase

    // Movement
    public static double walkSpeed = 0.19, runSpeed = 0.28, sprintSpeed = 0.33;
    public static double closeBoost = 1.15;         // speed multiplier inside closeRange (pressure near the player)
    public static double closeRange = 6;
    public static double separationRadius = 1.1;    // crowd spacing
    public static double separationPush = 0.035;    // horizontal push per tick per neighbour inside the radius
    public static int repathTicks = 10;             // chase path refresh
    public static int stuckTicks = 60;              // no progress this long while chasing -> repath / sidestep

    // Attack (wind-up telegraph -> active frames -> recovery)
    public static double attackReach = 1.7;         // horizontal distance to start an attack (and to land the hit)
    public static int windupTicks = 8, activeTicks = 3, recoverTicks = 9;
    public static int attackCooldownTicks = 10;
    public static float attackDamage = 6f;

    // Barricades
    public static int tearTicks = 32;               // one board per pull
    public static int breachGiveUpTicks = 400;      // stuck at a window this long -> respawn at a spawn point

    // Hit reaction
    public static int staggerTicks = 7;             // movement and attacks paused, current attack cancelled
    public static float staggerMinDamage = 1.5f;    // smaller hits only flinch (animation, no interruption)
    public static double knockback = 0.35;

    // Variants
    public static double crawlSpeed = 0.12;
    public static float crawlerHitFraction = 0.4f;  // one hit of >= 40% max health ...
    public static float crawlerChance = 0.35f;      // ... turns this share of survivors into crawlers

    // Death
    public static int corpseTicks = 20;             // vanilla death fall time before removal (LivingEntity.tickDeath)
}
