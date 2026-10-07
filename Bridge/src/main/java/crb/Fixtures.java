package crb;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.SlabBlock;
import net.minecraft.world.level.block.StairBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.SlabType;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Isolated, reversible superflat fixtures. Each fixture records every block and player value it changes and
 * restores exactly those on turn-off. The walking lane (|x| <= 1, z in [-2, 40]) is always kept clear so movement
 * and animation checks run identically with any fixture active.
 */
public final class Fixtures {
    public static final int FLOOR_Y = -61; // classic superflat: grass top layer

    public enum Kind {
        SHAPES("shapes", "Block shapes and collision"),
        COURSE("course", "Walking and step course"),
        ITEMS("items", "Held items and hotbar"),
        HUD("hud", "Armor, XP, health and hunger"),
        LIGHTING("lighting", "Night, torches and water"),
        PARTICLES("particles", "Particles"),
        SM64("sm64", "SM64 movement course (wall, stairs, tower)"),
        PP("pp", "Physics & portal course (surfaces, ramp, kicker, portal walls)"),
        ALL("all", "All features");

        public final String id, label;
        Kind(String id, String label) { this.id = id; this.label = label; }
        public static Kind byId(String id) { for (Kind k : values()) if (k.id.equals(id)) return k; return null; }
    }

    public static final class Active {
        final Kind kind;
        final Map<BlockPos, BlockState> originals = new LinkedHashMap<>();
        PlayerSnapshot player;
        Long previousTime;
        final List<BlockPos> emitters = new ArrayList<>();
        Active(Kind kind) { this.kind = kind; }
        public int blockCount() { return originals.size(); }

        void put(ServerLevel level, int x, int y, int z, BlockState state) {
            BlockPos pos = new BlockPos(x, y, z);
            if (Math.abs(x) <= 1 && z >= -2 && z <= 40 && y >= FLOOR_Y + 1) return; // keep the walking lane clear
            if (originals.size() >= ServerOps.MAX_FIXTURE_BLOCKS) return;
            originals.putIfAbsent(pos, level.getBlockState(pos));
            level.setBlock(pos, state, Block.UPDATE_ALL);
        }

        void finishShapes(ServerLevel level) {
            // Recompute connection-dependent shapes (fences, walls, panes) after placement.
            for (BlockPos pos : originals.keySet()) {
                BlockState s = level.getBlockState(pos);
                BlockState u = Block.updateFromNeighbourShapes(s, level, pos);
                if (u != s) level.setBlock(pos, u, Block.UPDATE_CLIENTS);
            }
        }

        void restore(MinecraftServer server, ServerPlayer p) {
            ServerLevel level = server.overworld();
            List<Map.Entry<BlockPos, BlockState>> list = new ArrayList<>(originals.entrySet());
            // Remove attachments (torches, lanterns) before supports: restore in reverse order.
            for (int i = list.size() - 1; i >= 0; i--) level.setBlock(list.get(i).getKey(), list.get(i).getValue(), Block.UPDATE_ALL);
            if (player != null) player.restore(p);
            if (previousTime != null) level.setDayTime(previousTime);
            originals.clear(); emitters.clear();
        }

        void tick(MinecraftServer server, ServerPlayer p, long tick) {
            if (emitters.isEmpty() || tick % 2 != 0) return;
            ServerLevel level = server.overworld();
            int i = 0;
            for (BlockPos e : emitters) {
                double x = e.getX() + 0.5, y = e.getY() + 0.6, z = e.getZ() + 0.5;
                switch (i++ % 3) {
                    case 0 -> level.sendParticles(ParticleTypes.FLAME, x, y, z, 2, 0.15, 0.1, 0.15, 0.01);
                    case 1 -> level.sendParticles(ParticleTypes.HAPPY_VILLAGER, x, y, z, 2, 0.3, 0.3, 0.3, 0.0);
                    default -> level.sendParticles(ParticleTypes.END_ROD, x, y, z, 1, 0.1, 0.2, 0.1, 0.02);
                }
            }
        }
    }

    record PlayerSnapshot(List<ItemStack> items, List<ItemStack> armor, ItemStack offhand, int selected,
                          float health, int food, float saturation, int level, float progress) {
        static PlayerSnapshot of(ServerPlayer p) {
            Inventory inv = p.getInventory();
            List<ItemStack> items = new ArrayList<>(), armor = new ArrayList<>();
            for (ItemStack s : inv.items) items.add(s.copy());
            for (ItemStack s : inv.armor) armor.add(s.copy());
            return new PlayerSnapshot(items, armor, inv.offhand.get(0).copy(), inv.selected, p.getHealth(),
                p.getFoodData().getFoodLevel(), p.getFoodData().getSaturationLevel(), p.experienceLevel, p.experienceProgress);
        }
        void restore(ServerPlayer p) {
            Inventory inv = p.getInventory();
            for (int i = 0; i < inv.items.size() && i < items.size(); i++) inv.items.set(i, items.get(i).copy());
            for (int i = 0; i < inv.armor.size() && i < armor.size(); i++) inv.armor.set(i, armor.get(i).copy());
            inv.offhand.set(0, offhand.copy());
            inv.selected = selected;
            p.setHealth(health);
            p.getFoodData().setFoodLevel(food);
            p.getFoodData().setSaturation(saturation);
            p.setExperienceLevels(level);
            p.setExperiencePoints(Math.round(progress * p.getXpNeededForNextLevel()));
            p.inventoryMenu.broadcastChanges();
        }
    }

    public static Active apply(Kind kind, MinecraftServer server, ServerPlayer p) {
        ServerLevel level = server.overworld();
        Active a = new Active(kind);
        switch (kind) {
            case SHAPES -> shapes(a, level);
            case COURSE -> course(a, level);
            case ITEMS -> items(a, p);
            case HUD -> hud(a, p);
            case LIGHTING -> lighting(a, level, 18000L);
            case PARTICLES -> particles(a, level);
            case SM64 -> sm64(a, level);
            case PP -> pp(a, level);
            case ALL -> {
                shapes(a, level); course(a, level); items(a, p); hud(a, p);
                lighting(a, level, 13000L); particles(a, level);
            }
        }
        a.finishShapes(level);
        p.inventoryMenu.broadcastChanges();
        return a;
    }

    private static void shapes(Active a, ServerLevel l) {
        int y = FLOOR_Y + 1;
        a.put(l, 3, y, 4, Blocks.OAK_SLAB.defaultBlockState());
        a.put(l, 4, y, 4, Blocks.OAK_SLAB.defaultBlockState().setValue(SlabBlock.TYPE, SlabType.TOP));
        a.put(l, 5, y, 4, Blocks.OAK_SLAB.defaultBlockState().setValue(SlabBlock.TYPE, SlabType.DOUBLE));
        a.put(l, 6, y, 4, Blocks.OAK_STAIRS.defaultBlockState().setValue(StairBlock.FACING, Direction.WEST));
        for (int z = 6; z <= 9; z++) a.put(l, 4, y, z, Blocks.OAK_FENCE.defaultBlockState());
        for (int z = 6; z <= 9; z++) a.put(l, 6, y, z, Blocks.COBBLESTONE_WALL.defaultBlockState());
        a.put(l, 3, y, 11, Blocks.GLASS.defaultBlockState());
        a.put(l, 4, y, 11, Blocks.RED_STAINED_GLASS.defaultBlockState());
        a.put(l, 5, y, 11, Blocks.OAK_LEAVES.defaultBlockState());
        a.put(l, 6, y, 11, Blocks.POPPY.defaultBlockState());
        a.put(l, 7, y, 11, Blocks.STONE_BRICKS.defaultBlockState());
        a.put(l, 7, y + 1, 11, Blocks.GOLD_BLOCK.defaultBlockState());
        // A small cover block the gravity gun can grab next to the lane.
        a.put(l, 2, y, 3, Blocks.OAK_PLANKS.defaultBlockState());
    }

    private static void course(Active a, ServerLevel l) {
        int y = FLOOR_Y + 1;
        a.put(l, -4, y, 3, Blocks.OAK_SLAB.defaultBlockState());
        a.put(l, -4, y, 4, Blocks.OAK_PLANKS.defaultBlockState());
        a.put(l, -4, y, 5, Blocks.OAK_STAIRS.defaultBlockState().setValue(StairBlock.FACING, Direction.SOUTH));
        a.put(l, -4, y + 1, 5, Blocks.OAK_SLAB.defaultBlockState());
        for (int z = 6; z <= 8; z++) { a.put(l, -4, y, z, Blocks.OAK_PLANKS.defaultBlockState()); a.put(l, -4, y + 1, z, Blocks.OAK_PLANKS.defaultBlockState()); }
    }

    /**
     * SM64 Steve Movement course: the walking lane is the runway (+Z); a stone-brick wall across its end for wall kicks
     * and bonks, a two-wall chimney for chained wall kicks, a staircase up to a platform and a tall tower for
     * ground pounds and hard landings.
     */
    private static void sm64(Active a, ServerLevel l) {
        int y = FLOOR_Y + 1;
        BlockState wall = Blocks.STONE_BRICKS.defaultBlockState(), plank = Blocks.OAK_PLANKS.defaultBlockState();
        for (int x = -6; x <= 6; x++) for (int h = 0; h < 9; h++) a.put(l, x, y + h, 46, wall);       // end wall
        for (int z = 8; z <= 18; z++) for (int h = 0; h < 12; h++) { a.put(l, 9, y + h, z, wall); a.put(l, 13, y + h, z, wall); } // chimney
        for (int k = 0; k < 6; k++) for (int x = -7; x <= -5; x++) {
            for (int h = 0; h < k; h++) a.put(l, x, y + h, 4 + k, plank);
            a.put(l, x, y + k, 4 + k, Blocks.OAK_STAIRS.defaultBlockState().setValue(StairBlock.FACING, Direction.SOUTH));
        }
        for (int z = 10; z <= 14; z++) for (int x = -9; x <= -3; x++) for (int h = 0; h < 6; h++) a.put(l, x, y + h, z, plank); // platform
        for (int x = 5; x <= 7; x++) for (int z = 24; z <= 26; z++) for (int h = 0; h < 14; h++) a.put(l, x, y + h, z, Blocks.SMOOTH_STONE.defaultBlockState()); // tower
    }

    /**
     * Physics & Portal course. Lane floor (x -1..1, the top layer is replaced): stone z 4..12, packed ice z 14..22,
     * slime z 24..25, sand z 27..31, soul sand z 33..36, honey z 38..40. West ramp (x -8..-4): a 4-high platform
     * (z 2..5), a half-block slope down to z 13, a flat run, a kicker of half-block steps at z 17..19 and a gap. Portal walls: smooth
     * stone at x = 5 (z 8..12, faces the lane) and at z = 46 (x -3..3, faces the start).
     */
    private static void pp(Active a, ServerLevel l) {
        int y = FLOOR_Y + 1;
        BlockState stone = Blocks.SMOOTH_STONE.defaultBlockState(), slab = Blocks.SMOOTH_STONE_SLAB.defaultBlockState();
        for (int x = -1; x <= 1; x++) {
            for (int z = 4; z <= 12; z++) a.put(l, x, FLOOR_Y, z, Blocks.STONE.defaultBlockState());
            for (int z = 14; z <= 22; z++) a.put(l, x, FLOOR_Y, z, Blocks.PACKED_ICE.defaultBlockState());
            for (int z = 24; z <= 25; z++) a.put(l, x, FLOOR_Y, z, Blocks.SLIME_BLOCK.defaultBlockState());
            for (int z = 27; z <= 31; z++) a.put(l, x, FLOOR_Y, z, Blocks.SAND.defaultBlockState());
            for (int z = 33; z <= 36; z++) a.put(l, x, FLOOR_Y, z, Blocks.SOUL_SAND.defaultBlockState());
            for (int z = 38; z <= 40; z++) a.put(l, x, FLOOR_Y, z, Blocks.HONEY_BLOCK.defaultBlockState());
        }
        for (int x = -8; x <= -4; x++) {
            for (int z = 2; z <= 5; z++) for (int h = 0; h < 4; h++) a.put(l, x, y + h, z, stone);                   // platform, top at +4
            for (int k = 0; k < 8; k++) {                                                                                // slope: 3.5 .. 0.5
                double top = 3.5 - k * 0.5; int z = 6 + k; int full = (int) Math.floor(top);
                for (int h = 0; h < full; h++) a.put(l, x, y + h, z, stone);
                if (top - full > 0.25) a.put(l, x, y + full, z, slab);
            }
            a.put(l, x, y, 17, slab); a.put(l, x, y, 18, stone); a.put(l, x, y, 19, stone); a.put(l, x, y + 1, 19, slab); // kicker: half-block steps 0.5 / 1 / 1.5
            for (int z = 20; z <= 30; z++) a.put(l, x, FLOOR_Y, z, Blocks.PACKED_ICE.defaultBlockState());             // icy landing
        }
        for (int z = 8; z <= 12; z++) for (int h = 0; h < 4; h++) a.put(l, 5, y + h, z, stone);                        // portal wall A
        for (int x = -3; x <= 3; x++) for (int h = 0; h < 5; h++) a.put(l, x, y + h, 46, stone);                       // portal wall B
    }

    private static void ensurePlayerSnapshot(Active a, ServerPlayer p) { if (a.player == null) a.player = PlayerSnapshot.of(p); }

    private static void items(Active a, ServerPlayer p) {
        ensurePlayerSnapshot(a, p);
        Inventory inv = p.getInventory();
        ItemStack pick = new ItemStack(Items.GOLDEN_PICKAXE); pick.setDamageValue(20);
        ItemStack[] hotbar = { new ItemStack(Items.WOODEN_SHOVEL), new ItemStack(Items.DIAMOND_SWORD), new ItemStack(Items.BOW),
            new ItemStack(Items.APPLE, 5), new ItemStack(Items.TORCH, 16), new ItemStack(Items.OAK_PLANKS, 32), pick,
            new ItemStack(Items.COMPASS), ItemStack.EMPTY };
        for (int i = 0; i < 9; i++) inv.items.set(i, hotbar[i]);
        inv.offhand.set(0, new ItemStack(Items.SHIELD));
        inv.selected = 0;
    }

    private static void hud(Active a, ServerPlayer p) {
        ensurePlayerSnapshot(a, p);
        Inventory inv = p.getInventory();
        inv.armor.set(0, new ItemStack(Items.IRON_BOOTS));
        inv.armor.set(1, new ItemStack(Items.IRON_LEGGINGS));
        inv.armor.set(2, new ItemStack(Items.IRON_CHESTPLATE));
        inv.armor.set(3, new ItemStack(Items.IRON_HELMET));
        p.setHealth(13f);
        p.getFoodData().setFoodLevel(11);
        p.setExperienceLevels(7);
        p.setExperiencePoints(9);
    }

    private static void lighting(Active a, ServerLevel l, long time) {
        if (a.previousTime == null) a.previousTime = l.getDayTime();
        l.setDayTime(time);
        int y = FLOOR_Y + 1;
        int[][] torches = { { 3, 14 }, { 7, 14 }, { 3, 18 }, { 7, 18 }, { -3, 16 }, { -6, 12 } };
        for (int[] t : torches) a.put(l, t[0], y, t[1], Blocks.TORCH.defaultBlockState());
        a.put(l, 5, y, 20, Blocks.GLOWSTONE.defaultBlockState());
        a.put(l, -3, y, 20, Blocks.OAK_FENCE.defaultBlockState());
        a.put(l, -3, y + 1, 20, Blocks.LANTERN.defaultBlockState());
        for (int x = 3; x <= 6; x++) for (int z = 22; z <= 25; z++) a.put(l, x, FLOOR_Y, z, Blocks.WATER.defaultBlockState());
        a.put(l, -5, y, 24, Blocks.SEA_LANTERN.defaultBlockState());
    }

    private static void particles(Active a, ServerLevel l) {
        int y = FLOOR_Y + 1;
        a.put(l, 3, y, 7, Blocks.CAMPFIRE.defaultBlockState());
        a.put(l, -3, y, 9, Blocks.STONE.defaultBlockState());
        a.emitters.add(new BlockPos(-3, y, 9));
        a.emitters.add(new BlockPos(3, y + 1, 12));
        a.emitters.add(new BlockPos(-2, y + 1, 14));
    }
}
