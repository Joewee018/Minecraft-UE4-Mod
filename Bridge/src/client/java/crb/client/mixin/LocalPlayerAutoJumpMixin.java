package crb.client.mixin;

import crb.client.sm64.Sm64Controller;
import net.minecraft.client.player.LocalPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** SM64 Steve Movement: vanilla auto-jump must not fire a vanilla jump while SM64 owns the movement. */
@Mixin(LocalPlayer.class)
public abstract class LocalPlayerAutoJumpMixin {
    @Inject(method = "updateAutoJump", at = @At("HEAD"), cancellable = true, require = 0)
    private void crb$sm64NoAutoJump(float dx, float dz, CallbackInfo ci) {
        if (Sm64Controller.INSTANCE.active() || crb.client.pp.PPController.INSTANCE.active()) ci.cancel();
    }
}
