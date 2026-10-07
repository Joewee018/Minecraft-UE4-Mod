package crb.mixin;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Glass destroyed through Level.destroyBlock (Guns++ "setblock air destroy", arrows below) -> shatter event. */
@Mixin(Level.class)
public abstract class LevelDestroyMixin {
    @Inject(method = "destroyBlock(Lnet/minecraft/core/BlockPos;ZLnet/minecraft/world/entity/Entity;I)Z", at = @At("HEAD"))
    private void crb$shatter(BlockPos pos, boolean drop, Entity breaker, int limit, CallbackInfoReturnable<Boolean> cir) {
        Level self = (Level) (Object) this;
        if (!(self instanceof ServerLevel sl)) return;
        BlockState s = self.getBlockState(pos);
        if (crb.Shatter.isGlass(s)) crb.ShatterAccess.record(sl, pos, s);
    }
}
