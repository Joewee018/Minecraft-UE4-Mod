package crb.zm;

import crb.BridgeMod;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.decoration.ItemFrame;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.storage.loot.LootParams;
import net.minecraft.world.level.storage.loot.LootTable;
import net.minecraft.world.level.storage.loot.parameters.LootContextParamSets;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

import java.util.*;

/**
 * Zombies game mode (round-based survival, Black Ops 2 style, rebuilt on Minecraft mechanics). Server thread only.
 *
 * Arena (built in the dev world at ARENA_X/ARENA_Z): a lit spawn room with four barricaded windows, a buyable door to an
 * open courtyard with four more windows, wall-buy guns (Guns++ items in item frames), perk machines and a random weapon
 * crate. Rounds scale zombie count, health and speed; points come from hits, kills, headshots, melee kills and repairs.
 * Power-ups drop from kills. Going down ends the match unless the self-revive perk is owned. Guns, bullets and damage
 * are the real Guns++ datapack mechanics; this class only hands out its loot-table items and listens to damage.
 */
public final class ZombiesGame {
    public static final ZombiesGame INSTANCE = new ZombiesGame();
    public static final int ARENA_X = 96, ARENA_Z = 96;

    public enum Phase { OFF, PREPARE, ROUND, BREAK, GAME_OVER }
    public enum PowerUp { MAX_AMMO, INSTA_KILL, DOUBLE_POINTS, NUKE }

    /** Everything the client exporter needs, immutable (read on the client thread). */
    public record View(String phase, int round, int points, int kills, int headshots, int zombiesLeft, List<String> perks,
                       String prompt, int promptCost, boolean promptAfford, int instaKill, int doublePoints, String lastPowerUp,
                       long lastPowerUpTick, String crate, int roundTick, boolean gunsInstalled, String message, long messageTick,
                       List<int[]> zombieAnims, int bulletsFired, int heldMag, int zombieHits, int heldCapacity, int reserve) {}
    public static volatile View VIEW = new View("OFF", 0, 0, 0, 0, 0, List.of(), "", 0, false, 0, 0, "", 0, "", 0, false, "", 0, List.of(), 0, -1, 0, 0, 0);

    // ---- Guns++ catalogue (loot table id, display name, ammo loot table) ----
    record Gun(String id, String name, String ammo) {}
    static final Gun START_PISTOL = new Gun("gun_18", "Glock", "bullet_0");
    static final List<Gun> CRATE = List.of(
        new Gun("gun_23", "AK-47", "bullet_6"), new Gun("gun_24", "LMG", "bullet_6"), new Gun("gun_11", "AWP", "bullet_11"),
        new Gun("gun_8", "P90", "bullet_9"), new Gun("gun_20", "Tactical AR", "bullet_9"), new Gun("gun_32", "Dragon Drum Shotgun", "bullet_2"),
        new Gun("gun_30", "Automatic Sniper", "bullet_6"), new Gun("gun_17", "44 Magnum", "bullet_0"), new Gun("gun_15", "Dualies", "bullet_0"),
        new Gun("gun_34", "Tactical Shotgun", "bullet_5"), new Gun("gun_19", "Rocket Launcher", "bullet_19"), new Gun("gun_25", "Tesla Gun", ""),
        new Gun("gun_5", "Minigun", "bullet_5"), new Gun("gun_31", "Longarm Enforcer", "bullet_2"), new Gun("gun_14", "Dragon's Breath Shotgun", "bullet_14"));

    // ---- arena parts ----
    public static final class Window {
        public final BlockPos gap; public final Direction out; public final boolean courtyard; public int boards = 6;
        Window(BlockPos gap, Direction out, boolean courtyard) { this.gap = gap; this.out = out; this.courtyard = courtyard; }
        public Vec3 gapCenter() { return Vec3.atBottomCenterOf(gap).add(0, 0.5, 0); }
        public Vec3 approach() { return Vec3.atBottomCenterOf(gap.relative(out, 1)); }
        public Vec3 insidePoint() { return Vec3.atBottomCenterOf(gap.relative(out.getOpposite(), 2)); }
        public Vec3 spawn() { return Vec3.atBottomCenterOf(gap.relative(out, 9)); }
    }
    enum Kind { WALLBUY, PERK, CRATE, DOOR }
    static final class Station {
        final Kind kind; final BlockPos at; final String label; final int cost; final Gun gun; final String perk; final Direction face;
        Station(Kind kind, BlockPos at, String label, int cost, Gun gun, String perk, Direction face) {
            this.kind = kind; this.at = at; this.label = label; this.cost = cost; this.gun = gun; this.perk = perk; this.face = face;
        }
    }

    final List<Window> windows = new ArrayList<>();
    final List<Station> stations = new ArrayList<>();
    final List<Entity> props = new ArrayList<>();          // item frames, crate display, power-up items
    final Map<ItemEntity, PowerUp> powerUps = new HashMap<>();
    final Random rng = new Random();

    Phase phase = Phase.OFF;
    BlockPos origin = BlockPos.ZERO;
    UUID playerId;
    int round, points, kills, headshots, toSpawn, alive, spawnCooldown, phaseTicks, roundTicks, powerUpsThisRound, repairPointsThisRound;
    int instaKill, doublePoints, crateTicks, interactCooldown;
    String crateGun = "", lastPowerUp = "", message = ""; long lastPowerUpTick, messageTick;
    Gun crateResult; ItemEntity crateDisplay;
    boolean doorOpen, secondWindUsed;
    boolean mapMode;            // played on an imported map (Maps menu): no arena is built, zombies come from all around
    BlockPos cratePos = BlockPos.ZERO;
    final MysteryBox box = new MysteryBox();
    Station crateStation;
    /** Wall-buys drawn as chalk outlines in Unreal (no frame, no item): position, facing, gun stack. Read by the client exporter. */
    public record WallBuy(int x, int y, int z, String face, String gunId, ItemStack stack) {}
    public static volatile List<WallBuy> WALLBUYS = List.of();
    final List<WallBuy> wallBuys = new ArrayList<>();
    /** Door opening animations in flight: display entities and the tick they finish. */
    final List<Object[]> doorAnims = new ArrayList<>();
    final Set<String> perks = new LinkedHashSet<>();
    ListTag savedInventory; GameType savedMode; net.minecraft.world.Difficulty savedDifficulty; Boolean savedMobSpawning; long tick;
    Station prompt; Window repairPrompt;
    final Set<UUID> bulletsSeen = new HashSet<>(); int bulletsFired, zombieHits; // diagnostics: Guns++ projectiles spawned / zombie hits

    public boolean running() { return phase != Phase.OFF; }
    public Window window(int i) { return i >= 0 && i < windows.size() ? windows.get(i) : null; }

    // ======================================================================== start / stop
    public String start(MinecraftServer server, ServerPlayer p) {
        if (running()) stop(server);
        ServerLevel level = p.serverLevel();
        boolean guns = lootExists(server, START_PISTOL.id);
        mapMode = crb.ServerOps.isMapWorld(server);
        playerId = p.getUUID();
        if (mapMode) {
            // The map is the arena: keep every block, add only a weapon crate in front of the player.
            origin = p.blockPosition();
            windows.clear(); stations.clear();
            for (Entity e : props) e.discard();
            props.clear();
            Vec3 f = Vec3.directionFromRotation(0, p.getYRot()).scale(3);
            BlockPos c = BlockPos.containing(p.getX() + f.x, p.getY(), p.getZ() + f.z);
            while (!level.getBlockState(c).isAir() && c.getY() < p.getBlockY() + 4) c = c.above();
            while (level.getBlockState(c.below()).isAir() && c.getY() > p.getBlockY() - 4) c = c.below();
            cratePos = c;
            box.place(level, List.of(c), 0);
            crateStation = new Station(Kind.CRATE, c.east(), "Random Weapon", 950, null, null, Direction.UP);
            stations.add(crateStation);
        } else {
            int y = level.getHeight(Heightmap.Types.MOTION_BLOCKING, ARENA_X, ARENA_Z);
            origin = new BlockPos(ARENA_X, y, ARENA_Z);
            buildArena(level);
            cratePos = box.pos();
        }
        round = 0; points = 500; kills = headshots = 0; perks.clear(); doorOpen = false; secondWindUsed = false;
        instaKill = doublePoints = crateTicks = 0; crateGun = ""; lastPowerUp = ""; powerUps.clear();
        savedInventory = p.getInventory().save(new ListTag());
        savedMode = p.gameMode.getGameModeForPlayer();
        // Zombies need a non-peaceful difficulty to exist and to hurt; vanilla spawns would wander into the arena.
        savedDifficulty = server.getWorldData().getDifficulty();
        if (savedDifficulty == net.minecraft.world.Difficulty.PEACEFUL) server.setDifficulty(net.minecraft.world.Difficulty.NORMAL, true);
        var spawning = server.getGameRules().getRule(net.minecraft.world.level.GameRules.RULE_DOMOBSPAWNING);
        savedMobSpawning = spawning.get(); spawning.set(false, server);
        p.getInventory().clearContent();
        p.setGameMode(GameType.ADVENTURE);
        p.getAttribute(Attributes.MAX_HEALTH).setBaseValue(20);
        p.setHealth(20); p.getFoodData().setFoodLevel(20);
        if (!mapMode) p.teleportTo(level, origin.getX() + 0.5, origin.getY(), origin.getZ() + 0.5, 0f, 0f);
        give(server, p, "minecraft", null, new ItemStack(Items.IRON_SWORD));               // combat knife
        if (guns) { giveGun(server, p, START_PISTOL, 4); } else { give(server, p, null, null, new ItemStack(Items.CROSSBOW)); give(server, p, null, null, new ItemStack(Items.ARROW, 64)); }
        phase = Phase.PREPARE; phaseTicks = 100; tick = 0;
        say("Survive.");
        publish(p);
        return guns ? "zombies started" : "zombies started (Guns++ not installed: crossbow fallback)";
    }

    public void stop(MinecraftServer server) {
        if (!running()) return;
        ServerPlayer p = player(server);
        ServerLevel level = p != null ? p.serverLevel() : server.overworld();
        killAllZombies(level, false);
        for (Entity e : props) e.discard();
        props.clear(); powerUps.clear(); crateDisplay = null;
        box.discard();
        for (Object[] d : doorAnims) for (Entity e : (Entity[]) d[0]) if (e != null) e.discard();
        doorAnims.clear();
        WALLBUYS = List.of(); wallBuys.clear();
        if (p != null) {
            p.getInventory().clearContent();
            if (savedInventory != null) p.getInventory().load(savedInventory);
            p.getAttribute(Attributes.MAX_HEALTH).setBaseValue(20);
            p.setHealth(20);
            p.removeAllEffects();
            if (savedMode != null) p.setGameMode(savedMode);
        }
        if (savedDifficulty != null) server.setDifficulty(savedDifficulty, true);
        if (savedMobSpawning != null) server.getGameRules().getRule(net.minecraft.world.level.GameRules.RULE_DOMOBSPAWNING).set(savedMobSpawning, server);
        savedDifficulty = null; savedMobSpawning = null;
        phase = Phase.OFF;
        VIEW = new View("OFF", 0, 0, 0, 0, 0, List.of(), "", 0, false, 0, 0, "", 0, "", 0, true, "", 0, List.of(), 0, -1, 0, 0, 0);
    }

    // ======================================================================== arena
    BlockPos rel(int x, int y, int z) { return origin.offset(x, y, z); }
    void set(ServerLevel l, int x, int y, int z, BlockState s) { l.setBlock(rel(x, y, z), s, Block.UPDATE_CLIENTS); }

    void buildArena(ServerLevel l) {
        windows.clear(); stations.clear();
        for (Entity e : props) e.discard();
        props.clear();
        l.getEntities((Entity) null, new AABB(rel(-20, -2, -20), rel(40, 20, 20)), e -> e instanceof ItemFrame || e instanceof ZmZombie || e instanceof net.minecraft.world.entity.Display).forEach(Entity::discard);
        wallBuys.clear();
        BlockState wall = Blocks.STONE_BRICKS.defaultBlockState(), floor = Blocks.POLISHED_ANDESITE.defaultBlockState(), air = Blocks.AIR.defaultBlockState();
        // clear + floor
        for (int x = -12; x <= 28; x++) for (int z = -12; z <= 12; z++) {
            set(l, x, -1, z, (x >= -8 && x <= 24 && z >= -8 && z <= 8) ? floor : Blocks.GRASS_BLOCK.defaultBlockState());
            for (int y = 0; y <= 5; y++) set(l, x, y, z, air);
        }
        // walls: spawn room x -8..8, courtyard x 8..24, z -8..8, height 4
        for (int y = 0; y < 4; y++) {
            for (int x = -8; x <= 24; x++) { set(l, x, y, -8, wall); set(l, x, y, 8, wall); }
            for (int z = -8; z <= 8; z++) { set(l, -8, y, z, wall); set(l, 8, y, z, wall); set(l, 24, y, z, wall); }
        }
        // spawn room ceiling: stone bricks, sea lanterns, a glass skylight
        for (int x = -8; x <= 8; x++) for (int z = -8; z <= 8; z++) {
            BlockState c = (Math.abs(x) <= 1 && Math.abs(z) <= 1) ? Blocks.GLASS.defaultBlockState()
                : (x % 4 == 0 && z % 4 == 0) ? Blocks.SEA_LANTERN.defaultBlockState() : wall;
            set(l, x, 4, z, c);
        }
        // courtyard: lanterns on posts
        for (int[] c : new int[][] { { 12, -5 }, { 12, 5 }, { 20, -5 }, { 20, 5 } }) { set(l, c[0], 0, c[1], Blocks.STONE_BRICK_WALL.defaultBlockState()); set(l, c[0], 1, c[1], Blocks.LANTERN.defaultBlockState()); }
        // glass panes in the courtyard walls (for shooting)
        for (int x : new int[] { 12, 20 }) { set(l, x, 2, -8, Blocks.GLASS_PANE.defaultBlockState()); set(l, x, 2, 8, Blocks.GLASS_PANE.defaultBlockState()); }
        // windows (gap = 1 wide x 2 high)
        addWindow(l, -3, -8, Direction.NORTH, false); addWindow(l, 3, -8, Direction.NORTH, false);
        addWindow(l, -8, 0, Direction.WEST, false); addWindow(l, 0, 8, Direction.SOUTH, false);
        addWindow(l, 24, -3, Direction.EAST, true); addWindow(l, 24, 3, Direction.EAST, true);
        addWindow(l, 16, -8, Direction.NORTH, true); addWindow(l, 16, 8, Direction.SOUTH, true);
        // door spawn room -> courtyard
        // Debris door: spruce boards with an iron grate across the middle (opens with an animation when bought).
        for (int z = -1; z <= 1; z++) for (int y = 0; y < 3; y++) set(l, 8, y, z, (y == 1 ? Blocks.IRON_BARS : Blocks.SPRUCE_PLANKS).defaultBlockState());
        stations.add(new Station(Kind.DOOR, rel(8, 1, 0), "Open Door", 750, null, null, Direction.WEST));
        // wall-buys (item frame showing the real Guns++ item)
        wallBuy(l, 0, 1, -7, Direction.SOUTH, new Gun("gun_0", "Revolver", "bullet_0"), 500);
        wallBuy(l, -7, 1, -4, Direction.EAST, new Gun("gun_10", "M1 Garand", "bullet_6"), 600);
        wallBuy(l, 7, 1, -5, Direction.WEST, new Gun("gun_7", "Shotgun", "bullet_2"), 1000);
        wallBuy(l, 12, 1, 7, Direction.NORTH, new Gun("gun_6", "Tommy Gun", "bullet_6"), 1200);
        wallBuy(l, 20, 1, -7, Direction.SOUTH, new Gun("gun_16", "AR 15", "bullet_6"), 1400);
        // perk machines (2-high coloured blocks)
        perk(l, -6, 6, Blocks.RED_CONCRETE, "Iron Gut", 2500);
        perk(l, 6, 6, Blocks.ORANGE_CONCRETE, "Second Wind", 500);
        perk(l, 22, 6, Blocks.BLUE_CONCRETE, "Quick Hands", 3000);
        perk(l, 22, -6, Blocks.LIME_CONCRETE, "Swift Step", 2000);
        // random weapon box: starts in the spawn room, can move to the courtyard spots
        box.place(l, List.of(rel(15, 0, 0), rel(-6, 0, -6), rel(16, 0, 5)), 1);
        crateStation = new Station(Kind.CRATE, box.pos().east(), "Random Weapon", 950, null, null, Direction.UP);
        stations.add(crateStation);
        WALLBUYS = List.copyOf(wallBuys);
    }

    void addWindow(ServerLevel l, int x, int z, Direction out, boolean courtyard) {
        Window w = new Window(rel(x, 0, z), out, courtyard);
        if (courtyard) w.boards = 6;
        windows.add(w);
        drawWindow(l, w);
    }

    void drawWindow(ServerLevel l, Window w) {
        BlockState planks = Blocks.OAK_PLANKS.defaultBlockState(), fence = Blocks.OAK_FENCE.defaultBlockState(), air = Blocks.AIR.defaultBlockState();
        l.setBlock(w.gap, w.boards >= 4 ? planks : w.boards >= 1 ? fence : air, Block.UPDATE_ALL);
        l.setBlock(w.gap.above(), w.boards >= 6 ? planks : w.boards >= 3 ? fence : air, Block.UPDATE_ALL);
    }

    void wallBuy(ServerLevel l, int x, int y, int z, Direction face, Gun gun, int cost) {
        BlockPos pos = rel(x, y, z);
        stations.add(new Station(Kind.WALLBUY, pos, gun.name, cost, gun, null, face));
        // BO2-style chalk drawing: no visible frame or item; Unreal draws a white outline traced from the gun's icon.
        ItemStack shown = gunStack(l.getServer(), gun);
        if (!shown.isEmpty()) wallBuys.add(new WallBuy(pos.getX(), pos.getY(), pos.getZ(), face.getName(), gun.id, shown));
    }

    void perk(ServerLevel l, int x, int z, Block block, String name, int cost) {
        set(l, x, 0, z, block.defaultBlockState()); set(l, x, 1, z, block.defaultBlockState());
        set(l, x, 2, z, Blocks.REDSTONE_LAMP.defaultBlockState().setValue(net.minecraft.world.level.block.RedstoneLampBlock.LIT, true));
        stations.add(new Station(Kind.PERK, rel(x, 1, z), name, cost, null, name, Direction.UP));
    }

    public boolean isInside(Vec3 p) {
        if (mapMode) return true;
        double x = p.x - origin.getX(), z = p.z - origin.getZ();
        if (z <= -8 || z >= 9 || p.y < origin.getY() - 1 || p.y > origin.getY() + 4) return false;
        return (x > -8 && x < 8) || (doorOpen && x >= 8 && x < 24);
    }

    // ======================================================================== tick
    public void tick(MinecraftServer server) {
        if (!running()) return;
        tick++;
        ServerPlayer p = player(server);
        if (p == null) { stop(server); return; }
        ServerLevel level = p.serverLevel();
        p.getFoodData().setFoodLevel(20);
        if (instaKill > 0) instaKill--;
        if (doublePoints > 0) doublePoints--;
        if (interactCooldown > 0) interactCooldown--;
        if (tick % 20 == 0) applyPerks(p);
        if (tick % 5 == 0) autoReload(server, p);
        tickBox(server, p);
        tickDoors(level);
        tickPowerUps(server, level, p);
        for (Entity e : level.getEntitiesOfClass(net.minecraft.world.entity.AreaEffectCloud.class, p.getBoundingBox().inflate(6), e -> e.getTags().contains("gz_projectile")))
            if (bulletsSeen.add(e.getUUID())) bulletsFired++;
        if (bulletsSeen.size() > 4096) bulletsSeen.clear();
        switch (phase) {
            case PREPARE, BREAK -> { if (--phaseTicks <= 0) startRound(level); }
            case ROUND -> tickRound(level, p);
            case GAME_OVER -> { if (--phaseTicks <= 0) stop(server); }
            default -> { }
        }
        findPrompt(p);
        if (tick % 2 == 0) publish(p);
    }

    void startRound(ServerLevel level) {
        round++;
        toSpawn = round <= 4 ? new int[] { 6, 8, 13, 18 }[round - 1] : Math.min(60, 18 + 3 * (round - 4));
        spawnCooldown = 40; roundTicks = 0; powerUpsThisRound = 0; repairPointsThisRound = 0;
        phase = Phase.ROUND;
        level.playSound(null, origin, SoundEvents.RAID_HORN.value(), SoundSource.HOSTILE, 3f, 0.7f);
        say("Round " + round);
    }

    float zombieHealth() {
        // Black Ops: 150 + 100 per round to 9, then x1.1 per round; scaled 1/20 to Minecraft health
        double h = round < 10 ? 150 + 100 * (round - 1) : 950 * Math.pow(1.1, round - 9);
        return (float) Math.max(6, h / 20.0);
    }

    void tickRound(ServerLevel level, ServerPlayer p) {
        roundTicks++;
        List<ZmZombie> zs = zombies(level);
        alive = zs.size();
        if (toSpawn > 0 && alive < 24 && --spawnCooldown <= 0) {
            spawnCooldown = Math.max(10, 40 - round * 3);
            List<Window> active = windows.stream().filter(w -> !w.courtyard || doorOpen).toList();
            Window w = active.isEmpty() ? null : active.get(rng.nextInt(active.size()));
            Vec3 s = w != null ? w.spawn() : mapSpawn(level, p);
            ZmZombie z = s == null ? null : BridgeMod.ZM_ZOMBIE.create(level);
            if (z != null) {
                z.window = w != null ? windows.indexOf(w) : -1;
                z.inside = w == null;
                z.moveTo(s.x, s.y, s.z, w != null ? w.out.getOpposite().toYRot() : rng.nextFloat() * 360f, 0);
                z.getAttribute(Attributes.MAX_HEALTH).setBaseValue(zombieHealth());
                z.setHealth(zombieHealth());
                double runner = Math.min(1.0, Math.max(0, (round - 3) * 0.25));
                double speed = rng.nextDouble() < runner ? (round >= 8 && rng.nextBoolean() ? ZmConfig.sprintSpeed : ZmConfig.runSpeed) : ZmConfig.walkSpeed;
                z.applyConfig(speed);
                level.addFreshEntity(z);
                toSpawn--;
            }
        }
        // unstick: no progress for 30 s outside -> back to a spawn
        for (ZmZombie z : zs) {
            boolean travelling = z.state == ZmZombie.ZState.CHASE || z.state == ZmZombie.ZState.BREACH || z.state == ZmZombie.ZState.SEARCH;
            if (travelling && z.position().distanceToSqr(z.lastPos) < 0.04) z.stuckTicks++; else { z.stuckTicks = 0; z.lastPos = z.position(); }
            if (z.stuckTicks > 600) {
                Window w = window(z.window);
                if (w != null) { Vec3 s = w.spawn(); z.teleportTo(s.x, s.y, s.z); z.inside = false; z.setState(ZmZombie.ZState.BREACH); }
                else { Vec3 s = mapSpawn(level, p); if (s != null) z.teleportTo(s.x, s.y, s.z); }
                z.stuckTicks = 0;
            }
        }
        if (toSpawn == 0 && alive == 0) {
            phase = Phase.BREAK; phaseTicks = 200;
            level.playSound(null, origin, SoundEvents.BELL_BLOCK, SoundSource.MASTER, 2f, 0.6f);
            say("Round " + round + " survived");
        }
    }

    /** Map mode: a standable surface spot 16-30 blocks from the player (never over the void). */
    Vec3 mapSpawn(ServerLevel level, ServerPlayer p) {
        for (int tries = 0; tries < 12; tries++) {
            double a = rng.nextDouble() * Math.PI * 2, d = 16 + rng.nextDouble() * 14;
            int x = (int) Math.floor(p.getX() + Math.cos(a) * d), z = (int) Math.floor(p.getZ() + Math.sin(a) * d);
            // Prefer the floor at the player's level (inside buildings), else the top surface.
            for (int dy = 3; dy >= -6; dy--) {
                BlockPos b = new BlockPos(x, p.getBlockY() + dy, z);
                if (level.getBlockState(b.below()).isSolidRender(level, b.below()) && level.getBlockState(b).isAir() && level.getBlockState(b.above()).isAir())
                    return Vec3.atBottomCenterOf(b);
            }
            int y = level.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, x, z);
            if (y > level.getMinBuildHeight() + 1 && Math.abs(y - p.getY()) < 24) return new Vec3(x + 0.5, y, z + 0.5);
        }
        return null;
    }

    List<ZmZombie> zombies(ServerLevel level) {
        return level.getEntitiesOfClass(ZmZombie.class, mapMode ? new AABB(origin).inflate(256, 96, 256) : new AABB(rel(-40, -8, -40), rel(60, 20, 40)), Entity::isAlive);
    }

    void killAllZombies(ServerLevel level, boolean award) {
        for (ZmZombie z : level.getEntitiesOfClass(ZmZombie.class, mapMode ? new AABB(origin).inflate(256, 96, 256) : new AABB(rel(-60, -20, -60), rel(80, 40, 60)), e -> true)) {
            if (award) z.kill(); else z.discard();
        }
    }

    public void tearBoard(Window w, ZmZombie z) {
        if (w.boards <= 0) return;
        w.boards--;
        ServerLevel l = (ServerLevel) z.level();
        drawWindow(l, w);
        l.playSound(null, w.gap, SoundEvents.ZOMBIE_BREAK_WOODEN_DOOR, SoundSource.HOSTILE, 0.8f, 1f);
    }

    // ======================================================================== interaction (F)
    void findPrompt(ServerPlayer p) {
        prompt = null; repairPrompt = null;
        Vec3 eye = p.getEyePosition(), look = p.getLookAngle();
        double best = 9;
        for (Station s : stations) {
            if (s.kind == Kind.DOOR && doorOpen) continue;
            if (s.kind != Kind.DOOR && isCourtyard(s.at) && !doorOpen) continue;
            Vec3 c = Vec3.atCenterOf(s.at);
            double d = c.distanceToSqr(eye);
            if (d < best && c.subtract(eye).normalize().dot(look) > 0.35) { best = d; prompt = s; }
        }
        if (prompt == null)
            for (Window w : windows) {
                if (w.courtyard && !doorOpen) continue;
                if (w.boards < 6 && w.gapCenter().distanceToSqr(p.position().add(0, 0.5, 0)) < 5.0 && isInside(p.position())) { repairPrompt = w; break; }
            }
    }

    boolean isCourtyard(BlockPos b) { return !mapMode && b.getX() - origin.getX() > 8; }

    public String interact(MinecraftServer server, ServerPlayer p) {
        if (!running() || phase == Phase.GAME_OVER) return "no game";
        findPrompt(p);
        if (repairPrompt != null) {
            Window w = repairPrompt;
            if (interactCooldown > 0) return "repairing";
            interactCooldown = 15;
            w.boards++;
            drawWindow(p.serverLevel(), w);
            p.serverLevel().playSound(null, w.gap, SoundEvents.WOOD_PLACE, SoundSource.BLOCKS, 1f, 1f);
            if (repairPointsThisRound < 500) { addPoints(10); repairPointsThisRound += 10; }
            return "repaired";
        }
        Station s = prompt;
        if (s == null) {
            // Diagnostics: the closest station and how it was judged (distance^2 < 9, facing dot > 0.35).
            Station best = null; double bd = 1e9; Vec3 eye = p.getEyePosition(), look = p.getLookAngle();
            for (Station st : stations) { double d = Vec3.atCenterOf(st.at).distanceToSqr(eye); if (d < bd) { bd = d; best = st; } }
            if (best == null) return "nothing here";
            double dot = Vec3.atCenterOf(best.at).subtract(eye).normalize().dot(look);
            return String.format("nothing here (nearest %s d2=%.1f dot=%.2f courtyard=%b door=%b)", best.label, bd, dot, isCourtyard(best.at), doorOpen);
        }
        if (interactCooldown > 0) return "wait";
        interactCooldown = 20;
        int cost = s.cost;
        boolean ammoOnly = false;
        if (s.kind == Kind.WALLBUY && owns(p, s.gun)) { cost = s.cost / 2; ammoOnly = true; }
        if (s.kind == Kind.PERK && perks.contains(s.perk)) return "already have " + s.perk;
        if (s.kind == Kind.CRATE && box.phase() == MysteryBox.Phase.OFFER) {
            ItemStack got = box.take(p.serverLevel());
            if (crateResult != null && !got.isEmpty()) { giveGun(server, p, crateResult, 3); say(crateResult.name); return "took " + crateResult.name; }
            return "box empty";
        }
        if (s.kind == Kind.CRATE && box.busy()) return "box busy";
        if (points < cost) { deny(p); return "not enough points"; }
        points -= cost;
        switch (s.kind) {
            case WALLBUY -> { if (ammoOnly) giveAmmo(server, p, s.gun, 4); else giveGun(server, p, s.gun, 3); }
            case PERK -> { perks.add(s.perk); applyPerks(p); if (s.perk.equals("Iron Gut")) p.setHealth(p.getMaxHealth()); say(s.perk); }
            case DOOR -> openDoor(p.serverLevel());
            case CRATE -> {
                crateResult = CRATE.get(rng.nextInt(CRATE.size()));
                boolean omen = !mapMode && box.spots.size() > 1 && box.uses >= 3 && rng.nextFloat() < 0.2f;
                box.open(p.serverLevel(), omen);
            }
        }
        p.serverLevel().playSound(null, p.blockPosition(), SoundEvents.EXPERIENCE_ORB_PICKUP, SoundSource.PLAYERS, 1f, 0.8f);
        return "bought " + s.label;
    }

    /** BO2: an empty magazine reloads by itself (Guns++'s own reload: the player's ggunz:reload/start). */
    void autoReload(MinecraftServer server, ServerPlayer p) {
        CompoundTag d = p.getMainHandItem().getTagElement("gz_data");
        if (d == null || !d.contains("capacity") || d.getInt("bullets") > 0) return;
        reload(server, p);
    }

    public String reload(MinecraftServer server, ServerPlayer p) {
        CompoundTag d = p.getMainHandItem().getTagElement("gz_data");
        if (d == null || !d.contains("capacity")) return "not holding a gun";
        if (p.getTags().contains("gz_reloading")) return "already reloading";
        if (d.getInt("bullets") >= d.getInt("capacity")) return "magazine full";
        var src = p.createCommandSourceStack().withSuppressedOutput().withPermission(2);
        p.addTag("gz_mainhand"); p.removeTag("gz_offhand");
        server.getCommands().performPrefixedCommand(src, "function ggunz:reload/start");
        return p.getTags().contains("gz_reloading") ? "reloading" : "reload refused";
    }

    void deny(ServerPlayer p) { p.serverLevel().playSound(null, p.blockPosition(), SoundEvents.VILLAGER_NO, SoundSource.PLAYERS, 1f, 1f); }

    void openDoor(ServerLevel l) {
        doorOpen = true;
        // Opening animation: every door block becomes a display entity that slides into the walls (halves left/right,
        // the middle column up), with dust; the blocks themselves are gone at once so the way is open.
        List<Entity> parts = new ArrayList<>();
        for (int z = -1; z <= 1; z++) for (int y = 0; y < 3; y++) {
            BlockPos b = rel(8, y, z);
            BlockState st = l.getBlockState(b);
            if (st.isAir()) continue;
            l.setBlock(b, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
            Entity d = MysteryBox.blockDisplay(l, st, Vec3.atLowerCornerOf(b), MysteryBox.transform(0, 0, 0, 1, 1, 1, 0));
            if (d == null) continue;
            parts.add(d);
            float dz = z == 0 ? 0f : z * 2.2f, dy = z == 0 ? 3.2f : 0f;
            final Entity part = d; final float fz = dz, fy = dy;
            pending.add(new Object[] { tick + 2, (Runnable) () -> MysteryBox.animate(part, MysteryBox.transform(0, fy, fz, 1, 1, 1, 0), 22) });
        }
        doorAnims.add(new Object[] { parts.toArray(new Entity[0]), tick + 40 });
        l.sendParticles(net.minecraft.core.particles.ParticleTypes.CAMPFIRE_COSY_SMOKE, origin.getX() + 8.5, origin.getY() + 1.5, origin.getZ() + 0.5, 12, 0.3, 0.8, 1.0, 0.01);
        l.sendParticles(net.minecraft.core.particles.ParticleTypes.POOF, origin.getX() + 8.5, origin.getY() + 0.5, origin.getZ() + 0.5, 20, 0.3, 0.6, 1.2, 0.02);
        l.playSound(null, rel(8, 1, 0), SoundEvents.IRON_DOOR_OPEN, SoundSource.BLOCKS, 2f, 0.6f);
        l.playSound(null, rel(8, 1, 0), SoundEvents.PISTON_EXTEND, SoundSource.BLOCKS, 1.5f, 0.5f);
        say("Courtyard open");
    }

    final List<Object[]> pending = new ArrayList<>(); // { dueTick, Runnable }

    void tickDoors(ServerLevel l) {
        for (int i = pending.size() - 1; i >= 0; i--) if ((long) pending.get(i)[0] <= tick) { ((Runnable) pending.get(i)[1]).run(); pending.remove(i); }
        for (int i = doorAnims.size() - 1; i >= 0; i--) {
            Object[] d = doorAnims.get(i);
            if ((long) d[1] > tick) continue;
            for (Entity e : (Entity[]) d[0]) e.discard();
            doorAnims.remove(i);
        }
    }

    void tickBox(MinecraftServer server, ServerPlayer p) {
        ServerLevel l = p.serverLevel();
        MysteryBox.Phase before = box.phase();
        ItemStack result = crateResult != null ? gunStack(server, crateResult) : ItemStack.EMPTY;
        box.tick(l, i -> { Gun g = CRATE.get(Math.floorMod(i * 7 + 3, CRATE.size())); crateGun = g.name; return gunStack(server, g); }, result, crateResult != null ? crateResult.name : "");
        MysteryBox.Phase now = box.phase();
        if (now == MysteryBox.Phase.LEAVING && before != now) { addPoints(950); say("The box is moving!"); crateGun = ""; }
        if (now == MysteryBox.Phase.OFFER && before != now && crateResult != null) crateGun = crateResult.name;
        if (now == MysteryBox.Phase.IDLE || now == MysteryBox.Phase.GONE || now == MysteryBox.Phase.CLOSING) crateGun = "";
        // the box moved: move its buy station with it
        if (crateStation != null && box.pos() != null && !crateStation.at.equals(box.pos().east()) && now == MysteryBox.Phase.IDLE) {
            stations.remove(crateStation);
            crateStation = new Station(Kind.CRATE, box.pos().east(), "Random Weapon", 950, null, null, Direction.UP);
            stations.add(crateStation);
            cratePos = box.pos();
        }
    }

    // ======================================================================== perks / points / power-ups
    void applyPerks(ServerPlayer p) {
        p.getAttribute(Attributes.MAX_HEALTH).setBaseValue(perks.contains("Iron Gut") ? 40 : 20);
        if (perks.contains("Swift Step")) p.addEffect(new MobEffectInstance(MobEffects.MOVEMENT_SPEED, 60, 0, false, false));
        if (perks.contains("Quick Hands")) p.addEffect(new MobEffectInstance(MobEffects.DIG_SPEED, 60, 1, false, false));
        if (!perks.isEmpty()) p.addEffect(new MobEffectInstance(MobEffects.REGENERATION, 60, 0, false, false));
    }

    void addPoints(int n) { points += doublePoints > 0 ? n * 2 : n; }

    /** Fabric ALLOW_DAMAGE: points per hit, insta-kill, headshot bookkeeping. */
    public boolean onDamage(Entity victim, DamageSource src, float amount) {
        if (!running() || !(victim instanceof ZmZombie z)) return true;
        boolean shot = src.typeHolder().unwrapKey().map(k -> k.location().equals(new ResourceLocation("ggunz", "shot"))).orElse(false);
        boolean byPlayer = shot || src.getEntity() instanceof Player;
        if (!byPlayer) return true;
        z.headshot = shot && z.getTags().contains("gz_headshot");
        zombieHits++;
        addPoints(10);
        if (instaKill > 0) { z.setHealth(0.01f); }
        return true;
    }

    /** Fabric AFTER_DEATH for zombies. */
    public void onZombieDeath(ZmZombie z, DamageSource src) {
        if (!running()) return;
        kills++;
        boolean melee = src.getDirectEntity() instanceof Player;
        if (melee) addPoints(130);
        else if (z.headshot) { addPoints(100); headshots++; }
        else addPoints(60);
        if (phase == Phase.ROUND && powerUpsThisRound < 4 && rng.nextFloat() < 0.04f) dropPowerUp((ServerLevel) z.level(), z.position());
    }

    void dropPowerUp(ServerLevel l, Vec3 at) {
        PowerUp k = PowerUp.values()[rng.nextInt(PowerUp.values().length)];
        ItemStack icon = new ItemStack(switch (k) { case MAX_AMMO -> Items.IRON_BLOCK; case INSTA_KILL -> Items.SKELETON_SKULL; case DOUBLE_POINTS -> Items.GOLD_BLOCK; case NUKE -> Items.TNT; });
        ItemEntity e = new ItemEntity(l, at.x, at.y + 0.6, at.z, icon);
        e.setNoGravity(true); e.setNeverPickUp(); e.setGlowingTag(true); e.setDeltaMovement(Vec3.ZERO);
        l.addFreshEntity(e); props.add(e); powerUps.put(e, k); powerUpsThisRound++;
    }

    void tickPowerUps(MinecraftServer server, ServerLevel l, ServerPlayer p) {
        Iterator<Map.Entry<ItemEntity, PowerUp>> it = powerUps.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry<ItemEntity, PowerUp> e = it.next();
            ItemEntity item = e.getKey();
            item.setDeltaMovement(Vec3.ZERO);
            if (item.isRemoved() || item.getAge() > 26 * 20) { item.discard(); it.remove(); continue; }
            if (item.position().distanceTo(p.position()) < 1.6) {
                PowerUp k = e.getValue();
                switch (k) {
                    case MAX_AMMO -> { for (Gun g : ownedGuns(p)) giveAmmo(server, p, g, 4); for (ItemStack st : p.getInventory().items) fillMagazine(st); }
                    case INSTA_KILL -> instaKill = 30 * 20;
                    case DOUBLE_POINTS -> doublePoints = 30 * 20;
                    case NUKE -> { for (ZmZombie z : zombies(l)) z.kill(); addPoints(400); }
                }
                lastPowerUp = switch (k) { case MAX_AMMO -> "Max Ammo"; case INSTA_KILL -> "Insta-Kill"; case DOUBLE_POINTS -> "Double Points"; case NUKE -> "Nuke"; };
                lastPowerUpTick = tick;
                l.playSound(null, p.blockPosition(), SoundEvents.PLAYER_LEVELUP, SoundSource.PLAYERS, 1f, 1.2f);
                item.discard(); it.remove();
            }
        }
    }

    /** Fabric ALLOW_DEATH for the player: self-revive perk, else game over (death cancelled; match ends). */
    public boolean onPlayerDeath(ServerPlayer p) {
        if (!running() || !p.getUUID().equals(playerId) || phase == Phase.GAME_OVER) return true;
        if (perks.contains("Second Wind") && !secondWindUsed) {
            secondWindUsed = true; perks.clear(); applyPerks(p);
            p.setHealth(p.getMaxHealth());
            p.addEffect(new MobEffectInstance(MobEffects.DAMAGE_RESISTANCE, 100, 4, false, false));
            for (ZmZombie z : zombies(p.serverLevel())) if (z.distanceTo(p) < 6) z.knockback(1.5, p.getX() - z.getX(), p.getZ() - z.getZ());
            say("Second Wind!");
            return false;
        }
        phase = Phase.GAME_OVER; phaseTicks = 200;
        p.setHealth(p.getMaxHealth());
        killAllZombies(p.serverLevel(), false);
        p.serverLevel().playSound(null, p.blockPosition(), SoundEvents.WITHER_DEATH, SoundSource.MASTER, 1f, 0.6f);
        say("Game Over - you survived " + Math.max(0, round - 1) + " rounds");
        publish(p);
        return false;
    }

    // ======================================================================== Guns++ items
    boolean lootExists(MinecraftServer server, String table) {
        return server.getLootData().getLootTable(new ResourceLocation("ggunz", table)) != LootTable.EMPTY;
    }

    List<ItemStack> loot(MinecraftServer server, String table) {
        // Guns: ggunz:gun_N (the charged crossbow that fires; ggunz:items/gun_N is only its crafted carrot form).
        // Ammo: ggunz:items/bullet_N (clocks with gz_data.bullet_type, consumed by the datapack's reload).
        LootTable t = server.getLootData().getLootTable(new ResourceLocation("ggunz", table));
        if (t == LootTable.EMPTY) return List.of();
        // Same context as "/loot give @s loot ...": origin + this entity, so the gun's score-based lore (@s bullets) resolves.
        ServerPlayer pl = player(server);
        if (pl == null) return t.getRandomItems(new LootParams.Builder(server.overworld()).create(LootContextParamSets.EMPTY));
        return t.getRandomItems(new LootParams.Builder(pl.serverLevel())
            .withParameter(net.minecraft.world.level.storage.loot.parameters.LootContextParams.ORIGIN, pl.position())
            .withOptionalParameter(net.minecraft.world.level.storage.loot.parameters.LootContextParams.THIS_ENTITY, pl)
            .create(LootContextParamSets.CHEST));
    }

    /** The gun's display/inventory form (ggunz:items/gun_N, a carrot on a stick): what Guns++ hands out unloaded. */
    ItemStack gunStack(MinecraftServer server, Gun g) { List<ItemStack> l = loot(server, "items/" + g.id); return l.isEmpty() ? ItemStack.EMPTY : l.get(0).copy(); }

    /**
     * The loaded, firing form. Guns++'s ggunz:gun_N table yields the charged crossbow only while the holder's gz_bullets
     * score is above 0 (otherwise its carrot form), exactly how the datapack swaps them after a reload. BO2 hands guns
     * out loaded, so the score is set to the magazine size first and the magazine NBT filled to match.
     */
    ItemStack loadedGun(MinecraftServer server, ServerPlayer p, Gun g) {
        ItemStack carrot = gunStack(server, g);
        CompoundTag d = carrot.getTagElement("gz_data");
        int cap = d != null ? d.getInt("capacity") : 0;
        var board = server.getScoreboard();
        var obj = board.getObjective("gz_bullets");
        if (cap <= 0 || obj == null) return carrot;
        board.getOrCreatePlayerScore(p.getScoreboardName(), obj).setScore(cap);
        List<ItemStack> l = loot(server, g.id);
        ItemStack gun = l.isEmpty() ? carrot : l.get(0).copy();
        fillMagazine(gun);
        return gun;
    }

    void giveGun(MinecraftServer server, ServerPlayer p, Gun g, int ammoRolls) {
        ItemStack st = loadedGun(server, p, g);
        if (st.isEmpty()) return;
        if (perks.contains("Quick Hands")) halveReload(st);
        fillMagazine(st); // Guns++ loot comes with an empty magazine; BO2 guns come loaded
        give(server, p, null, null, st);
        giveAmmo(server, p, g, ammoRolls);
    }

    void giveAmmo(MinecraftServer server, ServerPlayer p, Gun g, int rolls) {
        if (g.ammo.isEmpty()) return;
        for (int i = 0; i < rolls; i++) for (ItemStack st : loot(server, "items/" + g.ammo)) give(server, p, null, null, st);
    }

    static void fillMagazine(ItemStack st) {
        CompoundTag d = st.getTagElement("gz_data");
        if (d != null && d.contains("capacity")) d.putInt("bullets", d.getInt("capacity"));
    }

    void halveReload(ItemStack st) {
        CompoundTag d = st.getTagElement("gz_data");
        if (d != null && d.contains("reload_time")) d.putDouble("reload_time", Math.max(0.5, d.getDouble("reload_time") / 2));
    }

    void give(MinecraftServer server, ServerPlayer p, String ns, String id, ItemStack st) {
        if (!p.getInventory().add(st)) p.drop(st, false);
    }

    boolean owns(ServerPlayer p, Gun g) { return ownedGuns(p).stream().anyMatch(o -> o.id.equals(g.id)); }

    List<Gun> ownedGuns(ServerPlayer p) {
        List<Gun> all = new ArrayList<>(CRATE);
        all.add(START_PISTOL);
        for (Station s : stations) if (s.gun != null) all.add(s.gun);
        List<Gun> out = new ArrayList<>();
        MinecraftServer server = p.getServer();
        for (ItemStack st : p.getInventory().items) {
            CompoundTag d = st.getTagElement("gz_data");
            if (d == null || !d.contains("id")) continue;
            int id = d.getInt("id");
            for (Gun g : all) {
                ItemStack ref = gunStack(server, g);
                CompoundTag rd = ref.getTagElement("gz_data");
                if (rd != null && rd.getInt("id") == id && out.stream().noneMatch(o -> o.id.equals(g.id))) { out.add(g); break; }
            }
        }
        return out;
    }

    // ======================================================================== export
    void say(String m) { message = m; messageTick = tick; }

    ServerPlayer player(MinecraftServer server) { return playerId == null ? null : server.getPlayerList().getPlayer(playerId); }

    void publish(ServerPlayer p) {
        String pr = ""; int cost = 0;
        if (repairPrompt != null) { pr = "Hold F to rebuild barrier"; }
        else if (prompt != null) {
            Station s = prompt; cost = s.cost;
            switch (s.kind) {
                case WALLBUY -> { if (owns(p, s.gun)) { cost = s.cost / 2; pr = "Press F to buy ammo for " + s.label; } else pr = "Press F to buy " + s.label; }
                case PERK -> pr = perks.contains(s.perk) ? "You have " + s.perk : "Press F to buy " + s.perk;
                case CRATE -> {
                    if (box.phase() == MysteryBox.Phase.OFFER) { pr = "Press F to take " + (crateResult != null ? crateResult.name : "the weapon"); cost = 0; }
                    else if (box.busy()) { pr = ""; cost = 0; }
                    else pr = "Press F for a Random Weapon";
                }
                case DOOR -> pr = "Press F to open the door";
            }
        }
        List<int[]> anims = new ArrayList<>();
        if (p != null) for (ZmZombie z : zombies(p.serverLevel())) anims.add(new int[] { z.getId(), z.anim, z.inside ? 1 : 0 });
        VIEW = new View(phase.name(), round, points, kills, headshots, toSpawn + alive, List.copyOf(perks), pr, cost, points >= cost,
            instaKill / 20, doublePoints / 20, lastPowerUp, lastPowerUpTick, crateGun, roundTicks, true, message, messageTick, anims,
            bulletsFired, heldMag(p), zombieHits, heldCapacity(p), reserve(p));
    }

    public long tickCount() { return tick; }
    public String boxPhase() { return running() ? box.phase().name() : "OFF"; }

    static int heldCapacity(ServerPlayer p) {
        if (p == null) return 0;
        CompoundTag d = p.getMainHandItem().getTagElement("gz_data");
        return d == null ? 0 : d.getInt("capacity");
    }

    /** Reserve ammo for the held gun: Guns++ ammo clocks (gz_data.bullet_type) matching that gun's ammo type. */
    int reserve(ServerPlayer p) {
        if (p == null) return 0;
        CompoundTag d = p.getMainHandItem().getTagElement("gz_data");
        if (d == null || !d.contains("id")) return 0;
        String gid = "gun_" + d.getInt("id"), ammo = null;
        for (Gun g : CRATE) if (g.id.equals(gid)) ammo = g.ammo;
        if (START_PISTOL.id.equals(gid)) ammo = START_PISTOL.ammo;
        for (Station st : stations) if (st.gun != null && st.gun.id.equals(gid)) ammo = st.gun.ammo;
        if (ammo == null || ammo.isEmpty()) return 0;
        int type = Integer.parseInt(ammo.substring(ammo.indexOf('_') + 1)), n = 0;
        for (ItemStack st : p.getInventory().items) {
            CompoundTag a = st.getTagElement("gz_data");
            if (st.is(Items.CLOCK) && a != null && a.contains("bullet_type") && a.getInt("bullet_type") == type) n += st.getCount();
        }
        return n;
    }

    static int heldMag(ServerPlayer p) {
        if (p == null) return -1;
        CompoundTag d = p.getMainHandItem().getTagElement("gz_data");
        return d == null || !d.contains("bullets") ? -1 : d.getInt("bullets");
    }

    // ======================================================================== test support
    public String debug(MinecraftServer server, ServerPlayer p, com.google.gson.JsonObject a) {
        if (a.has("points")) points = a.get("points").getAsInt();
        if (a.has("round") && running()) { round = a.get("round").getAsInt() - 1; killAllZombies(p.serverLevel(), false); toSpawn = 0; phase = Phase.BREAK; phaseTicks = 1; }
        if (a.has("spawnZombie")) {
            // Test support: one zombie already inside, in front of the player (no barricade between).
            Vec3 at = p.position().add(Vec3.directionFromRotation(0, p.getYRot()).scale(a.get("spawnZombie").getAsDouble()));
            ZmZombie z = BridgeMod.ZM_ZOMBIE.create(p.serverLevel());
            if (z != null) {
                z.inside = true; z.window = 0;
                z.moveTo(at.x, p.getY(), at.z, p.getYRot() + 180f, 0);
                z.getAttribute(Attributes.MAX_HEALTH).setBaseValue(zombieHealth()); z.setHealth(zombieHealth());
                z.applyConfig(0.12);
                p.serverLevel().addFreshEntity(z);
                return "zombie " + z.getId();
            }
        }
        if (a.has("hurtZombie")) {
            ZmZombie near = null; double bd = 1e9;
            for (ZmZombie z : zombies(p.serverLevel())) { double d = z.distanceToSqr(p); if (d < bd) { bd = d; near = z; } }
            if (near != null) near.hurt(p.serverLevel().damageSources().playerAttack(p), a.get("hurtZombie").getAsFloat());
        }
        if (a.has("heal")) { p.setHealth(p.getMaxHealth()); p.invulnerableTime = 0; }
        if (a.has("hold")) { killAllZombies(p.serverLevel(), false); phase = Phase.BREAK; phaseTicks = 20 * 3600; } // test: freeze between rounds
        if (a.has("killAll")) for (ZmZombie z : zombies(p.serverLevel())) z.kill();
        if (a.has("powerUp")) { double d = a.has("distance") ? a.get("distance").getAsDouble() : 2; float yaw = a.has("yaw") ? a.get("yaw").getAsFloat() : p.getYRot(); dropPowerUp(p.serverLevel(), p.position().add(Vec3.directionFromRotation(0, yaw).scale(d))); }
        if (a.has("fillMags")) for (ItemStack st : p.getInventory().items) fillMagazine(st);
        if (a.has("breakWindow")) { Window w = windows.get(a.get("breakWindow").getAsInt()); w.boards = 0; drawWindow(p.serverLevel(), w); }
        if (a.has("lookAt")) {
            String what = a.get("lookAt").getAsString();
            for (Station s : stations) if (s.label.equalsIgnoreCase(what) || (s.perk != null && s.perk.equalsIgnoreCase(what))) {
                Vec3 c = Vec3.atCenterOf(s.at);
                Vec3 stand = c.add(Vec3.atLowerCornerOf(s.face == Direction.UP ? Direction.WEST.getNormal() : s.face.getNormal()).scale(1.6));
                p.teleportTo(p.serverLevel(), stand.x, origin.getY(), stand.z, 0, 0);
                p.lookAt(net.minecraft.commands.arguments.EntityAnchorArgument.Anchor.EYES, c);
                return "at " + s.label + "|" + c.x + "|" + c.y + "|" + c.z;
            }
        }
        return "ok";
    }
}
