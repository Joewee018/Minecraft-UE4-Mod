package crb;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.AreaEffectCloud;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.block.AbstractGlassBlock;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.IronBarsBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * Glass shatter events. Any glass block or pane (plain, stained, tinted) destroyed by a projectile - a Guns++ bullet
 * ({@code setblock ~ ~ ~ air destroy} from the datapack) or an arrow (LevelDestroyMixin / AbstractArrowMixin) - is
 * recorded with the block state it had, the direction of the shot and the light at the block, so Unreal can play the
 * shard burst. Server thread writes, client exporter reads (integrated server, same JVM): guarded by the class lock.
 */
public final class Shatter {
    public record Event(long seq, long tick, int x, int y, int z, int state, double dx, double dy, double dz, int light, String source) {}

    private static final ArrayDeque<Event> RECENT = new ArrayDeque<>();
    private static long seq;
    /** Direction hint from the projectile that is breaking a block right now (server thread). */
    static Vec3 hintDir; static String hintSource;
    public static volatile long total;

    public static boolean isGlass(BlockState s) {
        Block b = s.getBlock();
        return b instanceof AbstractGlassBlock || (b instanceof IronBarsBlock && b != Blocks.IRON_BARS);
    }

    public static void hint(Vec3 dir, String source) { hintDir = dir; hintSource = source; }
    public static void clearHint() { hintDir = null; hintSource = null; }

    /** Called before the glass at pos is destroyed (server side). */
    static void record(ServerLevel level, BlockPos pos, BlockState state) {
        Vec3 c = Vec3.atCenterOf(pos);
        Vec3 dir = hintDir; String src = hintSource;
        if (dir == null) {
            // Guns++ bullets are area_effect_clouds tagged gz_projectile, teleported along their rotation each tick.
            Entity best = null; double bd = 9;
            for (Entity e : level.getEntitiesOfClass(AreaEffectCloud.class, new AABB(pos).inflate(3), e -> e.getTags().contains("gz_projectile"))) {
                double d = e.distanceToSqr(c); if (d < bd) { bd = d; best = e; }
            }
            if (best != null) { dir = Vec3.directionFromRotation(best.getXRot(), best.getYRot()); src = "bullet"; }
        }
        if (dir == null) {
            Player p = level.getNearestPlayer(c.x, c.y, c.z, 64, false);
            dir = p != null ? c.subtract(p.getEyePosition()).normalize() : new Vec3(0, 0, 1);
            if (src == null) src = "other";
        }
        int light = (level.getBrightness(net.minecraft.world.level.LightLayer.SKY, pos) << 4) | level.getBrightness(net.minecraft.world.level.LightLayer.BLOCK, pos);
        synchronized (Shatter.class) {
            RECENT.addLast(new Event(++seq, level.getGameTime(), pos.getX(), pos.getY(), pos.getZ(), Block.getId(state), dir.x, dir.y, dir.z, light, src));
            while (RECENT.size() > 64) RECENT.removeFirst();
            total = seq;
        }
    }

    /** Events from the last two seconds (re-sent every state frame; Unreal dedupes by seq). */
    public static List<Event> recent(long gameTime) {
        synchronized (Shatter.class) {
            List<Event> out = new ArrayList<>();
            for (Event e : RECENT) if (gameTime - e.tick < 40 && gameTime >= e.tick) out.add(e);
            return out;
        }
    }
}
