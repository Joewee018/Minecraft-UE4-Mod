package crb.mixin;

import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Craft 64: Doom-scale incoming damage and green / blue armor absorption for a player in the mode. The damage source is
 * taken at the head of Player.hurt (server thread); actuallyHurt (called from it) gets the replaced amount.
 */
@Mixin(Player.class)
public abstract class PlayerC64DamageMixin {
    private static final ThreadLocal<DamageSource> CRB_SOURCE = new ThreadLocal<>();

    @Inject(method = "hurt", at = @At("HEAD"), require = 0)
    private void crb$c64Source(DamageSource source, float amount, CallbackInfoReturnable<Boolean> cir) { CRB_SOURCE.set(source); }

    @ModifyVariable(method = "actuallyHurt", at = @At(value = "HEAD"), argsOnly = true, ordinal = 0, require = 0)
    private float crb$c64Damage(float amount) {
        Object self = this;
        DamageSource src = CRB_SOURCE.get();
        if (!(self instanceof ServerPlayer sp) || src == null) return amount;
        return crb.c64.Craft64.INSTANCE.playerDamage(sp, src, amount);
    }
}
