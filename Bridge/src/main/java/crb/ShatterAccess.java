package crb;

/** Public entry for the mixin package (Shatter.record stays package-private to the bridge). */
public final class ShatterAccess {
    public static void record(net.minecraft.server.level.ServerLevel l, net.minecraft.core.BlockPos p, net.minecraft.world.level.block.state.BlockState s) { Shatter.record(l, p, s); }
}
