package crb.pp;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.StairBlock;
import net.minecraft.world.level.block.state.BlockState;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Physics & Portal debug props: a test ramp or a test surface patch placed in front of the player. Every changed
 * block is recorded and restored by clear() (also when the mod turns off), so the Minecraft world is left as it was.
 */
public final class PPProps {
    private PPProps() { }
    private static final Map<BlockPos, BlockState> SAVED = new LinkedHashMap<>();
    private static ServerLevel level;

    private static void put(ServerLevel l, BlockPos p, BlockState s) {
        if (!SAVED.containsKey(p)) SAVED.put(p.immutable(), l.getBlockState(p));
        l.setBlock(p, s, 2);
        level = l;
    }

    public static String spawn(ServerPlayer p, String kind) {
        ServerLevel l = p.serverLevel();
        Direction f = p.getDirection(), side = f.getClockWise();
        BlockPos base = p.blockPosition().relative(f, 3);
        switch (kind) {
            case "ramp" -> {
                // 3 wide: stairs up to 3 blocks then a lip (launches a fast roll)
                for (int w = -1; w <= 1; w++) for (int k = 0; k < 4; k++) {
                    BlockPos c = base.relative(side, w).relative(f, k);
                    for (int h = 0; h < k; h++) put(l, c.above(h), Blocks.SMOOTH_STONE.defaultBlockState());
                    put(l, c.above(k), Blocks.STONE_STAIRS.defaultBlockState().setValue(StairBlock.FACING, f));
                }
                return "test ramp placed";
            }
            case "ice", "slime", "sand", "soulsand", "honey" -> {
                BlockState s = switch (kind) {
                    case "ice" -> Blocks.PACKED_ICE.defaultBlockState(); case "slime" -> Blocks.SLIME_BLOCK.defaultBlockState();
                    case "sand" -> Blocks.SAND.defaultBlockState(); case "soulsand" -> Blocks.SOUL_SAND.defaultBlockState();
                    default -> Blocks.HONEY_BLOCK.defaultBlockState();
                };
                for (int w = -2; w <= 2; w++) for (int k = 0; k < 8; k++) put(l, base.relative(side, w).relative(f, k).below(), s);
                return "test surface (" + kind + ") placed";
            }
            default -> { return "unknown prop " + kind; }
        }
    }

    public static String clear() {
        int n = SAVED.size();
        if (level != null) for (Map.Entry<BlockPos, BlockState> e : SAVED.entrySet()) level.setBlock(e.getKey(), e.getValue(), 2);
        SAVED.clear();
        return "restored " + n + " blocks";
    }
}
