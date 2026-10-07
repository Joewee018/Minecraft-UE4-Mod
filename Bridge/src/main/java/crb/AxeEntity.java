package crb;

import net.minecraft.core.BlockPos;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ClientGamePacketListener;
import net.minecraft.network.protocol.game.ClientboundAddEntityPacket;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/**
 * God of War Unity port: the thrown axe, after the MIT-licensed iltenahmet/god-of-war-unity AxeController +
 * PlayerController (values from its DefaultScene): launched at 7 m/s with no gravity and a 30 rev spin, sticks to whatever
 * it hits (a block, or an entity it then rides along with), recalled at any time along a quadratic Bezier to the hand
 * over 1 s, and deals 30 damage to anything with health it touches while detached (1 s cooldown per target).
 * Damage uses vanilla DamageSources.playerAttack. Unreal draws an original axe mesh at {@link #VIEW}. Server thread only.
 */
public final class AxeEntity extends Entity {
    public enum Phase { FLYING, STUCK, RETURNING }
    public record View(boolean active, int phase, double x, double y, double z, float spin, float yaw, double travelled, int hits, long tick) {}
    public static volatile View VIEW = new View(false, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    public static final float DAMAGE = 30f;              // AxeController.damage
    public static final int COOLDOWN_TICKS = 20;         // AxeController.cooldownDuration = 1 s
    public static final double SPEED = 7.0 / 20.0;       // PlayerController.axeThrowSpeed 7 m/s (impulse on a 1 kg body)
    public static final int RETURN_TICKS = 20;           // PlayerController.axeRecallTime = 1 s
    public static final double MAX_RANGE = 64.0;          // no gravity in the original; stop after a long flight
    private UUID owner;
    private Phase phase = Phase.FLYING;
    private Vec3 vel = Vec3.ZERO, p0, p1;
    private double t, travelled;
    private float spin;
    private int hits, age, cooldown;
    private Entity stuckTo; private Vec3 stuckOffset;
    private final Map<UUID, Integer> lastHit = new HashMap<>();

    public AxeEntity(EntityType<? extends AxeEntity> type, Level level) { super(type, level); this.noPhysics = true; }

    /** Launch from {@code from} along {@code dir} (both validated by the caller). */
    public static AxeEntity launch(ServerPlayer p, Vec3 from, Vec3 dir) {
        AxeEntity a = new AxeEntity(BridgeMod.AXE, p.serverLevel());
        a.owner = p.getUUID();
        a.setPos(from.x, from.y, from.z);
        a.vel = dir.normalize().scale(SPEED);
        a.setYRot((float) Math.toDegrees(Math.atan2(-dir.x, dir.z)));
        p.serverLevel().addFreshEntity(a);
        p.serverLevel().playSound(null, p.blockPosition(), SoundEvents.TRIDENT_THROW, SoundSource.PLAYERS, 1f, 0.8f);
        a.publish();
        return a;
    }

    /** ReturnToPlayer: allowed in flight, stuck, or already returning (restarts from the current point like the original). */
    public void recall() {
        phase = Phase.RETURNING; t = 0; stuckTo = null;
        p0 = position(); p1 = null;
        level().playSound(null, blockPosition(), SoundEvents.TRIDENT_RETURN, SoundSource.PLAYERS, 1f, 1f);
    }

    public Phase phase() { return phase; }
    public int hits() { return hits; }

    @Override public void tick() {
        super.tick();
        if (level().isClientSide) return;
        age++;
        if (cooldown > 0) cooldown--;
        ServerPlayer p = owner == null ? null : ((ServerLevel) level()).getServer().getPlayerList().getPlayer(owner);
        if (p == null || p.level() != level() || age > 20 * 300) { discard(); publish(); return; }
        Vec3 pos = position();
        switch (phase) {
            case FLYING -> {
                Vec3 next = pos.add(vel);
                Entity hitEntity = damageAlong(pos, next, p);
                BlockHitResult bh = level().clip(new ClipContext(pos, next, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, this));
                if (hitEntity != null) {
                    // StickTo(other.transform): ride along with the entity it hit
                    stuckTo = hitEntity; stuckOffset = position().subtract(hitEntity.position()).add(vel.normalize().scale(0.3));
                    phase = Phase.STUCK;
                    next = hitEntity.position().add(stuckOffset);
                } else if (bh.getType() == HitResult.Type.BLOCK) {
                    next = bh.getLocation().subtract(vel.normalize().scale(0.15));
                    phase = Phase.STUCK;
                    level().playSound(null, BlockPos.containing(next), SoundEvents.TRIDENT_HIT_GROUND, SoundSource.PLAYERS, 1f, 1f);
                }
                travelled += next.distanceTo(pos);
                setPos(next.x, next.y, next.z);
                spin += 30f * 360f / 20f / 4f;  // visual spin (axeSpinRate torque), ~135 deg per tick
                if (travelled > MAX_RANGE || next.y < level().getMinBuildHeight() - 8) phase = Phase.STUCK;
            }
            case STUCK -> {
                if (stuckTo != null) {
                    if (!stuckTo.isAlive()) { stuckTo = null; }
                    else { Vec3 n = stuckTo.position().add(stuckOffset); setPos(n.x, n.y, n.z); }
                }
            }
            case RETURNING -> {
                Vec3 hand = p.getEyePosition().add(0, -0.55, 0).add(Vec3.directionFromRotation(0, p.getYRot() + 90f).scale(0.35));
                if (p1 == null) {
                    Vec3 mid = p0.add(hand).scale(0.5), d = hand.subtract(p0);
                    Vec3 side = d.cross(new Vec3(0, 1, 0)).normalize();
                    p1 = mid.add(side.scale(Math.min(4.0, 0.35 * d.length() + 1.0))).add(0, 1.0, 0);
                }
                t = Math.min(1.0, t + 1.0 / RETURN_TICKS);
                double u = 1 - t;
                Vec3 next = p0.scale(u * u).add(p1.scale(2 * u * t)).add(hand.scale(t * t));
                damageAlong(pos, next, p);
                travelled += next.distanceTo(pos);
                setPos(next.x, next.y, next.z);
                spin -= 45f;
                if (t >= 1.0) {
                    level().playSound(null, p.blockPosition(), SoundEvents.TRIDENT_RETURN, SoundSource.PLAYERS, 1f, 1.4f);
                    discard();
                }
            }
        }
        publish();
    }

    /** Damages living entities along the segment (30, at most once per target per cooldown). Returns the first one touched. */
    private Entity damageAlong(Vec3 a, Vec3 b, ServerPlayer p) {
        AABB box = new AABB(a, b).inflate(0.6);
        Entity first = null;
        for (Entity e : level().getEntities(this, box, e -> e instanceof LivingEntity && e != p && e.isAlive())) {
            AABB eb = e.getBoundingBox().inflate(0.3);
            if (!(eb.clip(a, b).isPresent() || eb.contains(b))) continue;
            if (first == null) first = e;
            Integer last = lastHit.get(e.getUUID());
            if (last != null && age - last < COOLDOWN_TICKS) continue;
            lastHit.put(e.getUUID(), age);
            e.hurt(level().damageSources().playerAttack(p), DAMAGE);
            hits++;
        }
        return first;
    }

    private void publish() {
        VIEW = new View(!isRemoved(), phase.ordinal(), getX(), getY(), getZ(), spin, getYRot(), travelled, hits, age);
    }

    @Override public boolean shouldBeSaved() { return false; }
    @Override protected void defineSynchedData() { }
    @Override protected void readAdditionalSaveData(CompoundTag tag) { }
    @Override protected void addAdditionalSaveData(CompoundTag tag) { }
    @Override public Packet<ClientGamePacketListener> getAddEntityPacket() { return new ClientboundAddEntityPacket(this); }
}
