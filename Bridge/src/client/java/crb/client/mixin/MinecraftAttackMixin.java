package crb.client.mixin;

import crb.client.HostInput;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

/** Vanilla only continues block breaking while its own mouse is grabbed; let the Unreal host's held button count. */
@Mixin(Minecraft.class)
public abstract class MinecraftAttackMixin {
    @ModifyVariable(method = "continueAttack", at = @At("HEAD"), argsOnly = true)
    private boolean crb$hostAttack(boolean leftClick) {
        return HostInput.overrideContinueAttack(leftClick);
    }
}
