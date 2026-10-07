package crb.client.mixin;

import crb.client.sm64.Sm64Controller;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** SM64 Steve Movement: the local player's travel step runs Sm64Controller instead of vanilla while the mod drives it. */
@Mixin(Player.class)
public abstract class PlayerTravelMixin {
    @Inject(method = "travel", at = @At("HEAD"), cancellable = true)
    private void crb$sm64Travel(Vec3 input, CallbackInfo ci) {
        Player self = (Player) (Object) this;
        // SM64 Steve Movement moves the local player instead of vanilla travel while active.
        if (crb.client.pp.PPController.INSTANCE.travel(self) || Sm64Controller.INSTANCE.travel(self)) ci.cancel();
    }
}
