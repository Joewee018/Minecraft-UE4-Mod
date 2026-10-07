package crb.mixin;

import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.projectile.AbstractArrow;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Arrows (and tridents) smash any glass they hit and fly on, slowed, instead of sticking in it. */
@Mixin(AbstractArrow.class)
public abstract class AbstractArrowMixin {
    @Inject(method = "onHitBlock", at = @At("HEAD"), cancellable = true)
    private void crb$smashGlass(BlockHitResult hit, CallbackInfo ci) {
        AbstractArrow self = (AbstractArrow) (Object) this;
        if (self.level().isClientSide) return;
        BlockPos pos = hit.getBlockPos();
        BlockState s = self.level().getBlockState(pos);
        if (!crb.Shatter.isGlass(s)) return;
        crb.Shatter.hint(self.getDeltaMovement().normalize(), "arrow");
        try { self.level().destroyBlock(pos, false, self); } finally { crb.Shatter.clearHint(); }
        self.setDeltaMovement(self.getDeltaMovement().scale(0.7));
        ci.cancel();
    }
}
