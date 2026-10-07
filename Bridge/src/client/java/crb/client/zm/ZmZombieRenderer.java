package crb.client.zm;

import crb.zm.ZmZombie;
import net.minecraft.client.model.geom.ModelLayers;
import net.minecraft.client.renderer.entity.AbstractZombieRenderer;
import net.minecraft.client.renderer.entity.EntityRendererProvider;
import net.minecraft.resources.ResourceLocation;

/** Vanilla zombie geometry with the Zombies-mode animation model; three looks for variety (zombie, husk, drowned). */
public final class ZmZombieRenderer extends AbstractZombieRenderer<ZmZombie, ZmZombieModel> {
    static final ResourceLocation[] SKINS = {
        new ResourceLocation("textures/entity/zombie/zombie.png"),
        new ResourceLocation("textures/entity/zombie/husk.png"),
        new ResourceLocation("textures/entity/zombie/drowned.png") };

    public ZmZombieRenderer(EntityRendererProvider.Context ctx) {
        super(ctx, new ZmZombieModel(ctx.bakeLayer(ModelLayers.ZOMBIE)),
            new ZmZombieModel(ctx.bakeLayer(ModelLayers.ZOMBIE_INNER_ARMOR)),
            new ZmZombieModel(ctx.bakeLayer(ModelLayers.ZOMBIE_OUTER_ARMOR)));
    }

    @Override public ResourceLocation getTextureLocation(net.minecraft.world.entity.monster.Zombie z) {
        int s = z instanceof ZmZombie zz ? zz.skin() : 0;
        return SKINS[Math.max(0, Math.min(SKINS.length - 1, s))];
    }
}
