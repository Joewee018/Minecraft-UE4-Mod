package crb;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.FlatLevelSource;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;

/**
 * Server-thread operations: debug fixtures, game mode, gravity gun, Herobrine (From The Fog) control. Every method here must run on the
 * integrated server thread (callers use server.execute). Results/events are reported through the sink.
 */
public final class ServerOps {
    public static final String TEST_WORLD_PREFIX = "crb-superflat";
    public static final int MAX_FIXTURE_BLOCKS = 4096;

    /** Immutable snapshot published for the client exporter (read from the client thread). */
    public record GravityView(boolean holding, int stateId, double x, double y, double z, double distance, long sinceTick) {}
    public static volatile GravityView gravityView = new GravityView(false, 0, 0, 0, 0, 0, 0);
    public static volatile String activeFixture = "";

    private volatile Consumer<JsonObject> events = e -> { };
    /** UUID of the player the Unreal host controls (set by the client side). */
    public static volatile UUID controlledPlayer;
    private Fixtures.Active fixture;
    private BlockState held;
    private BlockPos heldOrigin;
    private double heldDistance = 4.0;
    private long tick;
    private AxeEntity axe;
    private final java.util.Map<UUID, Long> meleeHits = new java.util.HashMap<>();

    public void setEventSink(Consumer<JsonObject> sink) { this.events = sink == null ? e -> { } : sink; }

    public static boolean isDisposableWorld(MinecraftServer server) {
        if (!Boolean.getBoolean("crb.devWorld")) return false;
        String name = server.getWorldData().getLevelName();
        ServerLevel level = server.overworld();
        return name != null && name.startsWith(TEST_WORLD_PREFIX) && level.getChunkSource().getGenerator() instanceof FlatLevelSource;
    }

    public static final String DEBUG_WORLD_PREFIX = "crb-debug";

    /** Any world this project created for itself (the superflat test world or the vanilla debug-mode world). */
    public static boolean isDevWorld(MinecraftServer server) {
        if (!Boolean.getBoolean("crb.devWorld")) return false;
        String name = server.getWorldData().getLevelName();
        return name != null && (name.startsWith(TEST_WORLD_PREFIX) || name.startsWith(DEBUG_WORLD_PREFIX) || isMapWorld(server));
    }

    /** A map imported through the Maps menu (Work\\mc\\saves\\crb-map-*): ops allowed, the Zombies arena is not. */
    public static boolean isMapWorld(MinecraftServer server) {
        if (!Boolean.getBoolean("crb.devWorld")) return false;
        return server.getWorldPath(net.minecraft.world.level.storage.LevelResource.ROOT).toAbsolutePath().normalize().getFileName().toString().startsWith("crb-map-");
    }

    static ServerPlayer player(MinecraftServer server, UUID id) {
        return server.getPlayerList().getPlayer(id);
    }

    public JsonObject handle(MinecraftServer server, UUID playerId, String op, JsonObject args) {
        ServerPlayer p = player(server, playerId);
        if (p == null) return err("no server player");
        boolean disposable = isDisposableWorld(server);
        if (op.startsWith("pp.")) {
            // Physics & Portal mod, server side: portals, physics objects, course, position reset.
            switch (op) {
                case "pp.shoot": return ok(crb.pp.Portals.INSTANCE.shoot(p, args.has("which") ? args.get("which").getAsInt() : 0));
                case "pp.clearPortals": crb.pp.Portals.INSTANCE.clear(); return ok("portals removed");
                case "pp.portal": {
                    if (!isDevWorld(server)) return err("pp.portal is limited to this project's dev worlds");
                    net.minecraft.core.BlockPos bp = new net.minecraft.core.BlockPos(args.get("x").getAsInt(), args.get("y").getAsInt(), args.get("z").getAsInt());
                    net.minecraft.core.Direction face = net.minecraft.core.Direction.byName(args.get("face").getAsString());
                    if (face == null) return err("bad face");
                    String m = crb.pp.Portals.INSTANCE.place(p.serverLevel(), args.get("which").getAsInt(), bp, face, p.getYRot());
                    return m.startsWith("portal ") ? ok(m) : err(m);
                }
                case "pp.object": {
                    JsonObject r = ok("physics object"); r.addProperty("uuid", crb.pp.Portals.INSTANCE.spawnObject(p, args.has("speed") ? args.get("speed").getAsDouble() : 8)); return r;
                }
                case "pp.prop": {
                    if (!isDevWorld(server)) return err("pp.prop is limited to this project's dev worlds");
                    return ok(crb.pp.PPProps.spawn(p, args.has("kind") ? args.get("kind").getAsString() : "ramp"));
                }
                case "pp.propClear": return ok(crb.pp.PPProps.clear());
                case "pp.cleanup": crb.pp.Portals.INSTANCE.cleanup(server); return ok("portals and physics objects removed");
                case "pp.home": {
                    if (!isDevWorld(server)) return err("pp.home is limited to this project's dev worlds");
                    p.teleportTo(0.5, Fixtures.FLOOR_Y + 1, 0.5); p.setDeltaMovement(Vec3.ZERO); return ok("reset world position");
                }
                default: return err("unknown op " + op);
            }
        }
        if (op.startsWith("ec.")) return crb.ec.EldenCombat.INSTANCE.op(p, op, args, isDevWorld(server));   // Minecraft x Elden Combat
        if (op.startsWith("c64.")) {
            if ((op.equals("c64.give") || op.equals("c64.drop") || op.equals("c64.hurt")) && !isDevWorld(server)) return err(op + " is limited to this project's dev worlds");
            return crb.c64.Craft64.INSTANCE.op(p, op, args);
        }
        switch (op) {
            case "world.info": {
                JsonObject r = ok("world");
                r.addProperty("levelName", server.getWorldData().getLevelName());
                r.addProperty("generator", server.overworld().getChunkSource().getGenerator().getClass().getSimpleName());
                r.addProperty("superflat", server.overworld().getChunkSource().getGenerator() instanceof FlatLevelSource);
                r.addProperty("disposable", disposable);
                r.addProperty("fixture", activeFixture);
                return r;
            }
            case "player.reset": {
                if (!disposable) return err("player.reset is limited to the disposable superflat test world");
                p.teleportTo(p.serverLevel(), 0.5, Fixtures.FLOOR_Y + 1, 0.5, 0f, 0f);
                p.setDeltaMovement(Vec3.ZERO);
                p.resetFallDistance();
                return ok("player reset");
            }
            case "fixture.list": {
                JsonObject r = ok("fixtures");
                JsonArray a = new JsonArray();
                for (Fixtures.Kind k : Fixtures.Kind.values()) { JsonObject o = new JsonObject(); o.addProperty("id", k.id); o.addProperty("label", k.label); a.add(o); }
                r.add("fixtures", a);
                return r;
            }
            case "fixture.toggle": {
                if (!disposable) return err("fixtures are limited to the disposable superflat test world");
                String id = args.has("id") ? args.get("id").getAsString() : "";
                Fixtures.Kind kind = Fixtures.Kind.byId(id);
                if (kind == null) return err("unknown fixture " + id);
                boolean wasActive = fixture != null && fixture.kind == kind;
                if (fixture != null) { fixture.restore(server, p); fixture = null; activeFixture = ""; }
                if (wasActive) {
                    JsonObject r = ok("fixture off: " + kind.label); r.addProperty("active", ""); return r;
                }
                fixture = Fixtures.apply(kind, server, p);
                activeFixture = kind.id;
                JsonObject r = ok("fixture on: " + kind.label + " (" + fixture.blockCount() + " blocks)");
                r.addProperty("active", kind.id);
                return r;
            }
            case "fixture.off": {
                if (fixture != null) { fixture.restore(server, p); fixture = null; activeFixture = ""; }
                return ok("fixtures off");
            }
            case "time.set": {
                if (!disposable) return err("time.set is limited to the disposable test world");
                p.serverLevel().setDayTime(args.get("time").getAsLong());
                return ok("time set");
            }
            case "gravity.grab": return gravityGrab(p);
            case "gravity.release": return gravityRelease(p, true);
            case "gravity.distance": {
                if (held == null) return err("not holding a block");
                double d = args.has("value") ? args.get("value").getAsDouble() : heldDistance + (args.has("delta") ? args.get("delta").getAsDouble() : 0);
                heldDistance = Math.max(2.0, Math.min(10.0, d));
                publishGravity(p);
                JsonObject r = ok("distance"); r.addProperty("distance", heldDistance); return r;
            }
            case "gamemode.set": {
                // Survival <-> creative (and spectator) for this project's own dev worlds, via vanilla setGameMode
                // (which syncs abilities and the client's game mode exactly like /gamemode).
                if (!isDevWorld(server)) return err("game mode switching is limited to this project's dev worlds");
                String m = args.has("mode") ? args.get("mode").getAsString() : "";
                GameType type = GameType.byName(m, null);
                if (type == null) return err("unknown game mode " + m);
                p.setGameMode(type);
                JsonObject r = ok("game mode " + type.getName()); r.addProperty("mode", type.getName()); return r;
            }
            case "player.kill": {
                // Test support (dev worlds only): vanilla /kill, so the client shows its real DeathScreen.
                if (!isDevWorld(server)) return err("player.kill is limited to this project's dev worlds");
                p.kill();
                return ok("player killed");
            }
            case "ui.test": {
                // Test support (dev worlds only): a vanilla /title and a chat line through the real command system.
                if (!isDevWorld(server)) return err("ui.test is limited to this project's dev worlds");
                var src = p.createCommandSourceStack().withPermission(4).withSuppressedOutput();
                server.getCommands().performPrefixedCommand(src, "title @s times 5 60 10");
                server.getCommands().performPrefixedCommand(src, "title @s subtitle {\"text\":\"Crossover-Rebuilt UI test\",\"color\":\"gray\"}");
                server.getCommands().performPrefixedCommand(src, "title @s title {\"text\":\"Herobrine is watching\",\"color\":\"red\"}");
                server.getCommands().performPrefixedCommand(src, "tellraw @s {\"text\":\"[Crossover] chat line from Java\",\"color\":\"yellow\"}");
                return ok("title and chat sent");
            }
            case "avatar.axe.throw": {
                // God of War Unity port (dev worlds only): PlayerController.ThrowAxe - launch from the hand toward the
                // point under the camera crosshair. Origin must be near the player; direction is normalised.
                if (!isDevWorld(server)) return err("avatar.axe is limited to this project's dev worlds");
                if (axe != null && !axe.isRemoved()) return err("axe already thrown; recall it first");
                Vec3 eye = p.getEyePosition();
                Vec3 from = eye;
                if (args.has("ox") && args.has("oy") && args.has("oz")) {
                    Vec3 o = new Vec3(args.get("ox").getAsDouble(), args.get("oy").getAsDouble(), args.get("oz").getAsDouble());
                    if (Double.isFinite(o.x) && Double.isFinite(o.y) && Double.isFinite(o.z) && o.distanceTo(eye) < 3.0) from = o;
                }
                Vec3 dir;
                if (args.has("dx") && args.has("dy") && args.has("dz")) dir = new Vec3(args.get("dx").getAsDouble(), args.get("dy").getAsDouble(), args.get("dz").getAsDouble());
                else dir = Vec3.directionFromRotation(args.has("pitch") ? args.get("pitch").getAsFloat() : p.getXRot(), args.has("yaw") ? args.get("yaw").getAsFloat() : p.getYRot());
                if (!Double.isFinite(dir.x) || !Double.isFinite(dir.y) || !Double.isFinite(dir.z) || dir.lengthSqr() < 1e-6) return err("bad throw direction");
                axe = AxeEntity.launch(p, from, dir);
                return ok("axe thrown");
            }
            case "avatar.axe.recall": {
                if (!isDevWorld(server)) return err("avatar.axe is limited to this project's dev worlds");
                if (axe == null || axe.isRemoved()) return err("no axe out");
                axe.recall();
                return ok("axe recalling");
            }
            case "gow.melee": {
                // Melee with the axe in hand (AxeController: attached + attacking): 30 damage to living entities in a
                // 2.6-block, 70-degree arc in front of the player, 1 s cooldown per target.
                if (!isDevWorld(server)) return err("gow.melee is limited to this project's dev worlds");
                if (axe != null && !axe.isRemoved()) return ok("axe not in hand");
                Vec3 eye = p.getEyePosition(), look = Vec3.directionFromRotation(0, p.getYRot());
                int n = 0;
                for (net.minecraft.world.entity.Entity e : p.serverLevel().getEntities(p, p.getBoundingBox().inflate(3.0),
                        e -> e instanceof net.minecraft.world.entity.LivingEntity && e.isAlive())) {
                    Vec3 to = e.getBoundingBox().getCenter().subtract(eye);
                    Vec3 flat = new Vec3(to.x, 0, to.z);
                    double dist = flat.length();
                    if (dist > 2.6 + e.getBbWidth() / 2 || Math.abs(to.y) > 2.2) continue;
                    if (dist > 0.3 && flat.normalize().dot(look) < Math.cos(Math.toRadians(70))) continue;
                    Long last = meleeHits.get(e.getUUID());
                    if (last != null && tick - last < AxeEntity.COOLDOWN_TICKS) continue;
                    meleeHits.put(e.getUUID(), tick);
                    e.hurt(p.serverLevel().damageSources().playerAttack(p), AxeEntity.DAMAGE);
                    n++;
                }
                JsonObject r = ok("melee"); r.addProperty("hits", n); return r;
            }
            case "gow.mutant.spawn": {
                if (!isDevWorld(server)) return err("gow.mutant is limited to this project's dev worlds");
                double dist = args.has("distance") ? Math.max(2, Math.min(24, args.get("distance").getAsDouble())) : 6;
                float yaw = args.has("yaw") ? args.get("yaw").getAsFloat() : p.getYRot();
                if (!Float.isFinite(yaw)) yaw = p.getYRot();
                Vec3 at = p.position().add(Vec3.directionFromRotation(0, yaw).scale(dist));
                MutantEntity m = BridgeMod.MUTANT.create(p.serverLevel());
                if (m == null) return err("cannot create mutant");
                m.moveTo(at.x, p.getY(), at.z, yaw + 180f, 0f);
                p.serverLevel().addFreshEntity(m);
                JsonObject r = ok("mutant spawned"); r.addProperty("uuid", m.getUUID().toString());
                r.addProperty("x", at.x); r.addProperty("y", p.getY()); r.addProperty("z", at.z); return r;
            }
            case "gow.mutant.clear": {
                if (!isDevWorld(server)) return err("gow.mutant is limited to this project's dev worlds");
                int n = 0;
                for (net.minecraft.world.entity.Entity e : p.serverLevel().getEntities(BridgeMod.MUTANT, p.getBoundingBox().inflate(256), e -> true)) { e.discard(); n++; }
                JsonObject r = ok("mutants removed"); r.addProperty("removed", n); return r;
            }
            case "test.spawn": {
                // Test support (dev worlds only): a no-AI mob at a fixed offset in front of the player.
                if (!isDevWorld(server)) return err("test.spawn is limited to this project's dev worlds");
                String id = args.has("type") ? args.get("type").getAsString() : "minecraft:pig";
                var type = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE.getOptional(new net.minecraft.resources.ResourceLocation(id));
                if (type.isEmpty()) return err("unknown entity type " + id);
                double dist = args.has("distance") ? Math.max(1, Math.min(24, args.get("distance").getAsDouble())) : 6;
                Vec3 look = Vec3.directionFromRotation(0, p.getYRot());
                Vec3 at = p.position().add(look.scale(dist));
                net.minecraft.world.entity.Entity e = type.get().create(p.serverLevel());
                if (e == null) return err("cannot create " + id);
                e.moveTo(at.x, p.getY(), at.z, p.getYRot() + 180f, 0f);
                if (e instanceof net.minecraft.world.entity.Mob m) m.setNoAi(true);
                p.serverLevel().addFreshEntity(e);
                JsonObject r = ok("spawned " + id); r.addProperty("uuid", e.getUUID().toString());
                r.addProperty("x", at.x); r.addProperty("y", p.getY()); r.addProperty("z", at.z); return r;
            }
            case "test.entity": {
                if (!isDevWorld(server)) return err("test.entity is limited to this project's dev worlds");
                if (!args.has("uuid")) return err("uuid required");
                net.minecraft.world.entity.Entity e = p.serverLevel().getEntity(UUID.fromString(args.get("uuid").getAsString()));
                JsonObject r = ok("entity");
                r.addProperty("exists", e != null);
                if (e instanceof net.minecraft.world.entity.LivingEntity le) {
                    r.addProperty("health", le.getHealth()); r.addProperty("maxHealth", le.getMaxHealth()); r.addProperty("alive", le.isAlive());
                }
                if (e != null && args.has("discard") && args.get("discard").getAsBoolean()) e.discard();
                return r;
            }
            case "test.glass": {
                // Test support (dev worlds only): a row of glass blocks/panes at eye height in front of the player.
                if (!isDevWorld(server)) return err("test.glass is limited to this project's dev worlds");
                String id = args.has("block") ? args.get("block").getAsString() : "minecraft:glass";
                var block = net.minecraft.core.registries.BuiltInRegistries.BLOCK.getOptional(new net.minecraft.resources.ResourceLocation(id));
                if (block.isEmpty() || !Shatter.isGlass(block.get().defaultBlockState())) return err("not a glass block: " + id);
                double dist = args.has("distance") ? Math.max(2, Math.min(12, args.get("distance").getAsDouble())) : 5;
                Vec3 look = Vec3.directionFromRotation(0, p.getYRot());
                BlockPos at = BlockPos.containing(p.getEyePosition().add(look.scale(dist)));
                BlockState gs = block.get().defaultBlockState();
                p.serverLevel().setBlock(at, gs, Block.UPDATE_ALL);
                p.serverLevel().setBlock(at.below(), Blocks.STONE.defaultBlockState(), Block.UPDATE_ALL);
                JsonObject r = ok("placed " + id); r.addProperty("x", at.getX()); r.addProperty("y", at.getY()); r.addProperty("z", at.getZ());
                r.addProperty("state", Block.getId(p.serverLevel().getBlockState(at))); return r;
            }
            case "test.arrow": {
                // Test support (dev worlds only): a real arrow fired from the player's eye at a block centre.
                if (!isDevWorld(server)) return err("test.arrow is limited to this project's dev worlds");
                Vec3 target = new Vec3(args.get("x").getAsDouble() + 0.5, args.get("y").getAsDouble() + 0.5, args.get("z").getAsDouble() + 0.5);
                net.minecraft.world.entity.projectile.Arrow a = new net.minecraft.world.entity.projectile.Arrow(p.serverLevel(), p);
                Vec3 eye = p.getEyePosition();
                a.setPos(eye.x, eye.y - 0.1, eye.z);
                Vec3 d = target.subtract(eye).normalize();
                a.shoot(d.x, d.y, d.z, 3.0f, 0f);
                a.pickup = net.minecraft.world.entity.projectile.AbstractArrow.Pickup.DISALLOWED;
                p.serverLevel().addFreshEntity(a);
                JsonObject r = ok("arrow fired"); r.addProperty("shatterBefore", Shatter.total); return r;
            }
            case "test.tp": {
                // Test support (dev worlds only): place the player (SM64 drops, wall kicks) with zero velocity.
                if (!isDevWorld(server)) return err("test.tp is limited to this project's dev worlds");
                double x = args.get("x").getAsDouble(), y = args.get("y").getAsDouble(), z = args.get("z").getAsDouble();
                float yaw = args.has("yaw") ? args.get("yaw").getAsFloat() : p.getYRot();
                p.teleportTo(p.serverLevel(), x, y, z, yaw, p.getXRot());
                p.setDeltaMovement(Vec3.ZERO);
                p.resetFallDistance();
                JsonObject r = ok("teleported"); r.addProperty("noFall", Sm64Server.noFall(p.getUUID())); return r;
            }
            case "test.health": {
                JsonObject r = ok("health"); r.addProperty("health", p.getHealth()); r.addProperty("noFall", Sm64Server.noFall(p.getUUID()));
                if (args.has("set") && isDevWorld(server)) p.setHealth(args.get("set").getAsFloat());
                return r;
            }
            case "test.blockAt": {
                if (!args.has("x")) return err("x,y,z required");
                BlockPos bp = new BlockPos(args.get("x").getAsInt(), args.get("y").getAsInt(), args.get("z").getAsInt());
                JsonObject r = ok(p.serverLevel().getBlockState(bp).toString()); r.addProperty("air", p.serverLevel().getBlockState(bp).isAir()); return r;
            }
            case "zm.start": {
                if (!isDevWorld(server)) return err("zombies mode is limited to this project's dev worlds and imported maps");
                if (held != null) gravityRelease(p, false);
                return ok(crb.zm.ZombiesGame.INSTANCE.start(server, p));
            }
            case "zm.stop": {
                boolean was = crb.zm.ZombiesGame.INSTANCE.running();
                crb.zm.ZombiesGame.INSTANCE.stop(server);
                return ok(was ? "zombies stopped" : "zombies not running");
            }
            case "zm.interact": return ok(crb.zm.ZombiesGame.INSTANCE.interact(server, p));
            case "zm.reload": return ok(crb.zm.ZombiesGame.INSTANCE.running() ? crb.zm.ZombiesGame.INSTANCE.reload(server, p) : "zombies not running");
            case "zm.debug": {
                if (!isDevWorld(server)) return err("zm.debug is limited to this project's dev worlds");
                if (!crb.zm.ZombiesGame.INSTANCE.running()) return err("zombies not running");
                return ok(crb.zm.ZombiesGame.INSTANCE.debug(server, p, args));
            }
            case "herobrine.status": return Herobrine.status(server, p);
            case "herobrine.config": return Herobrine.config(server, p, args);
            case "herobrine.summon": return Herobrine.summon(server, p, args);
            case "herobrine.clear": return Herobrine.clear(server, p);
            default: return err("unknown op " + op);
        }
    }

    // ---------------- gravity gun ----------------
    private JsonObject gravityGrab(ServerPlayer p) {
        if (held != null) return err("already holding a block");
        HitResult hit = p.pick(10.0, 1.0f, false);
        if (!(hit instanceof BlockHitResult bh) || hit.getType() != HitResult.Type.BLOCK) return err("no block in range");
        ServerLevel level = p.serverLevel();
        BlockPos pos = bh.getBlockPos();
        BlockState state = level.getBlockState(pos);
        if (state.isAir() || state.hasBlockEntity() || !state.getFluidState().isEmpty() || state.getDestroySpeed(level, pos) < 0 || state.is(Blocks.BEDROCK))
            return err("block is not eligible: " + state);
        if (!level.mayInteract(p, pos)) return err("not allowed to modify this block");
        held = state;
        heldOrigin = pos.immutable();
        heldDistance = Math.max(2.0, Math.min(10.0, p.getEyePosition().distanceTo(Vec3.atCenterOf(pos))));
        level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
        publishGravity(p);
        JsonObject r = ok("grabbed " + state);
        r.addProperty("stateId", Block.getId(state));
        r.addProperty("x", pos.getX()); r.addProperty("y", pos.getY()); r.addProperty("z", pos.getZ());
        return r;
    }

    private JsonObject gravityRelease(ServerPlayer p, boolean place) {
        if (held == null) return err("not holding a block");
        ServerLevel level = p.serverLevel();
        BlockState state = held;
        BlockPos placed = null;
        if (place) {
            Vec3 eye = p.getEyePosition(), look = p.getLookAngle();
            for (double d = heldDistance; d >= 1.5 && placed == null; d -= 0.5) {
                BlockPos c = BlockPos.containing(eye.add(look.scale(d)));
                if (level.getBlockState(c).canBeReplaced() && !c.equals(p.blockPosition()) && !c.equals(p.blockPosition().above())) placed = c;
            }
        }
        if (placed == null && heldOrigin != null && level.getBlockState(heldOrigin).canBeReplaced()) placed = heldOrigin;
        JsonObject r;
        if (placed != null) {
            level.setBlock(placed, Block.updateFromNeighbourShapes(state, level, placed), Block.UPDATE_ALL);
            r = ok("placed " + state);
            r.addProperty("x", placed.getX()); r.addProperty("y", placed.getY()); r.addProperty("z", placed.getZ());
        } else {
            Block.popResource(level, p.blockPosition(), new ItemStack(state.getBlock()));
            r = ok("dropped " + state + " as an item");
        }
        r.addProperty("stateId", Block.getId(state));
        held = null; heldOrigin = null;
        gravityView = new GravityView(false, 0, 0, 0, 0, 0, tick);
        return r;
    }

    private void publishGravity(ServerPlayer p) {
        if (held == null) { gravityView = new GravityView(false, 0, 0, 0, 0, 0, tick); return; }
        Vec3 c = p.getEyePosition().add(p.getLookAngle().scale(heldDistance));
        gravityView = new GravityView(true, Block.getId(held), c.x, c.y, c.z, heldDistance, tick);
    }

    /** Server tick: fixture emitters, held-block tracking. */
    public void tick(MinecraftServer server, UUID playerId) {
        tick++;
        ServerPlayer p = playerId == null ? null : player(server, playerId);
        if (fixture != null && p != null) fixture.tick(server, p, tick);
        if (held != null && p != null) publishGravity(p);
    }

    /** Host disconnected or world closing: never lose the held block, leave the arena as it was. */
    public void cleanup(MinecraftServer server, UUID playerId) {
        ServerPlayer p = playerId == null ? null : player(server, playerId);
        if (held != null) {
            if (p != null) gravityRelease(p, false);
            else if (heldOrigin != null) { server.overworld().setBlock(heldOrigin, held, Block.UPDATE_ALL); held = null; }
        }
        gravityView = new GravityView(false, 0, 0, 0, 0, 0, tick);
    }

    /** Fully reset (fixtures too) — used when the world closes. */
    public void shutdown(MinecraftServer server, UUID playerId) {
        cleanup(server, playerId);
        ServerPlayer p = playerId == null ? null : player(server, playerId);
        if (fixture != null && p != null) fixture.restore(server, p);
        fixture = null; activeFixture = "";
        if (axe != null && !axe.isRemoved()) axe.discard();
        axe = null;
        AxeEntity.VIEW = new AxeEntity.View(false, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    static JsonObject ok(String message) { JsonObject o = new JsonObject(); o.addProperty("ok", true); o.addProperty("message", message); return o; }
    static JsonObject err(String message) { JsonObject o = new JsonObject(); o.addProperty("ok", false); o.addProperty("message", message); return o; }
}
