package crb;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.MobCategory;

/** Common entry point: owns the server-thread operations (fixtures, game mode, gravity gun, Herobrine control). */
public final class BridgeMod implements ModInitializer {
    public static final String VERSION = "1.0.0";
    private static final ServerOps OPS = new ServerOps();

    /** Custom Avatar add-on: the thrown axe (transient, never saved; drawn by Unreal). */
    public static EntityType<AxeEntity> AXE;
    /** God of War Unity port: the Mutant enemy (100 health target). */
    public static EntityType<MutantEntity> MUTANT;
    /** Zombies mode: the round-based zombie (barricade breaker). */
    public static EntityType<crb.zm.ZmZombie> ZM_ZOMBIE;
    /** Zombies mode: the random weapon box (only ever shown through display entities, never placed). */
    public static net.minecraft.world.level.block.Block MYSTERY_BOX, MYSTERY_BOX_LID;

    public static ServerOps ops() { return OPS; }

    @Override
    public void onInitialize() {
        crb.c64.C64Items.register();
        ServerTickEvents.END_SERVER_TICK.register(crb.pp.Portals.INSTANCE::tick);
        ServerLifecycleEvents.SERVER_STOPPED.register(s -> { crb.pp.Portals.INSTANCE.clear(); crb.pp.Portals.ENABLED = false; });
        ServerTickEvents.END_SERVER_TICK.register(crb.c64.Craft64.INSTANCE::tick);
        // Minecraft x Elden Combat (inert until a player turns the mod on from the Unreal mod menu)
        ServerTickEvents.END_SERVER_TICK.register(crb.ec.EldenCombat.INSTANCE::tick);
        ServerLifecycleEvents.SERVER_STOPPING.register(s -> crb.ec.EldenCombat.INSTANCE.cleanupWorld(s));
        ServerLifecycleEvents.SERVER_STOPPED.register(s -> crb.ec.EldenCombat.INSTANCE.clear());
        net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents.AFTER_RESPAWN.register((oldP, newP, alive) -> crb.ec.EldenCombat.INSTANCE.onRespawn(newP));
        ServerLifecycleEvents.SERVER_STOPPED.register(s -> crb.c64.Craft64.INSTANCE.clear());
        net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents.AFTER_RESPAWN.register((oldP, newP, alive) -> crb.c64.Craft64.INSTANCE.onRespawn(newP));
        AXE = Registry.register(BuiltInRegistries.ENTITY_TYPE, new ResourceLocation("crossover_rebuilt", "axe"),
            EntityType.Builder.<AxeEntity>of(AxeEntity::new, MobCategory.MISC).sized(0.5f, 0.5f)
                .clientTrackingRange(8).updateInterval(1).noSave().build("crossover_rebuilt:axe"));
        MUTANT = Registry.register(BuiltInRegistries.ENTITY_TYPE, new ResourceLocation("crossover_rebuilt", "mutant"),
            EntityType.Builder.<MutantEntity>of(MutantEntity::new, MobCategory.MONSTER).sized(1.0f, 2.1f)
                .clientTrackingRange(8).build("crossover_rebuilt:mutant"));
        net.fabricmc.fabric.api.object.builder.v1.entity.FabricDefaultAttributeRegistry.register(MUTANT, MutantEntity.createAttributes());
        MYSTERY_BOX = Registry.register(BuiltInRegistries.BLOCK, new ResourceLocation("crossover_rebuilt", "mystery_box"),
            new net.minecraft.world.level.block.Block(net.minecraft.world.level.block.state.BlockBehaviour.Properties.of().noOcclusion().strength(-1f, 3600000f).noLootTable()));
        MYSTERY_BOX_LID = Registry.register(BuiltInRegistries.BLOCK, new ResourceLocation("crossover_rebuilt", "mystery_box_lid"),
            new net.minecraft.world.level.block.Block(net.minecraft.world.level.block.state.BlockBehaviour.Properties.of().noOcclusion().strength(-1f, 3600000f).noLootTable()));
        ZM_ZOMBIE = Registry.register(BuiltInRegistries.ENTITY_TYPE, new ResourceLocation("crossover_rebuilt", "zm_zombie"),
            EntityType.Builder.<crb.zm.ZmZombie>of(crb.zm.ZmZombie::new, MobCategory.MONSTER).sized(0.6f, 1.95f)
                .clientTrackingRange(8).noSave().build("crossover_rebuilt:zm_zombie"));
        net.fabricmc.fabric.api.object.builder.v1.entity.FabricDefaultAttributeRegistry.register(ZM_ZOMBIE, net.minecraft.world.entity.monster.Zombie.createAttributes());
        ServerTickEvents.END_SERVER_TICK.register(server -> OPS.tick(server, ServerOps.controlledPlayer));
        ServerTickEvents.START_SERVER_TICK.register(Sm64Server::tick);
        ServerTickEvents.END_SERVER_TICK.register(Sm64Server::tick);
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents.SERVER_STOPPED.register(s -> Sm64Server.clear());
        ServerTickEvents.END_SERVER_TICK.register(crb.zm.ZombiesGame.INSTANCE::tick);
        ServerLifecycleEvents.SERVER_STOPPING.register(server -> OPS.shutdown(server, ServerOps.controlledPlayer));
        ServerLifecycleEvents.SERVER_STOPPING.register(crb.zm.ZombiesGame.INSTANCE::stop);
        // Zombies mode hooks (no-ops unless a match is running).
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DAMAGE.register(
            (entity, source, amount) -> crb.zm.ZombiesGame.INSTANCE.onDamage(entity, source, amount));
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DEATH.register((entity, source) -> {
            if (entity instanceof crb.zm.ZmZombie z) crb.zm.ZombiesGame.INSTANCE.onZombieDeath(z, source);
        });
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DEATH.register((entity, source, amount) ->
            !(entity instanceof net.minecraft.server.level.ServerPlayer sp) || crb.zm.ZombiesGame.INSTANCE.onPlayerDeath(sp));
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DEATH.register((entity, source) -> {
            if (entity instanceof net.minecraft.server.level.ServerPlayer sp) crb.c64.Craft64.INSTANCE.onPlayerDeath(sp);
            if (entity instanceof net.minecraft.server.level.ServerPlayer sp) crb.ec.EldenCombat.INSTANCE.onPlayerDeath(sp);
        });
    }
}
