package crb.mixin;

import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** SM64 Steve Movement: no vanilla fall damage for a player the mod is driving (unless the setting re-enables it). */
@Mixin(Player.class)
public abstract class PlayerFallDamageMixin {
    @Inject(method = "causeFallDamage", at = @At("HEAD"), cancellable = true, require = 0)
    private void crb$sm64NoFall(CallbackInfoReturnable<Boolean> cir) {
        Object self = this;
        if (self instanceof ServerPlayer sp && (crb.Sm64Server.noFall(sp.getUUID()) || crb.c64.Craft64.INSTANCE.isOn(sp.getUUID()))) cir.setReturnValue(false);
    }
}
