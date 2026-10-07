package crb.mixin;

import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Minecraft x Elden Combat hook (inert unless the mod is ON for this player): dodge invincibility frames, parry, guard
 * and poise decide how much of an incoming hit goes through. A reduced hit is re-sent through vanilla Player.hurt, so
 * armor, effects, death and every other vanilla rule still apply to what is left.
 */
@Mixin(Player.class)
public abstract class PlayerECDamageMixin {
    private static final ThreadLocal<Boolean> CRB_EC_REENTRY = ThreadLocal.withInitial(() -> false);

    @Inject(method = "hurt", at = @At("HEAD"), cancellable = true, require = 0)
    private void crb$ecHurt(DamageSource source, float amount, CallbackInfoReturnable<Boolean> cir) {
        Object self = this;
        if (!(self instanceof ServerPlayer sp) || CRB_EC_REENTRY.get() || !crb.ec.EldenCombat.INSTANCE.isOn(sp.getUUID())) return;
        float left = crb.ec.EldenCombat.INSTANCE.incoming(sp, source, amount);
        if (left == amount) return;
        if (left <= 0) { cir.setReturnValue(false); return; }
        CRB_EC_REENTRY.set(true);
        try { cir.setReturnValue(sp.hurt(source, left)); } finally { CRB_EC_REENTRY.set(false); }
    }
}
