package crb.zm;

import net.minecraft.network.syncher.EntityDataAccessor;
import net.minecraft.network.syncher.EntityDataSerializers;
import net.minecraft.network.syncher.SynchedEntityData;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.ai.goal.FloatGoal;
import net.minecraft.world.entity.ai.goal.Goal;
import net.minecraft.world.entity.monster.Zombie;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.pathfinder.Path;
import net.minecraft.world.phys.Vec3;

import java.util.EnumSet;
import java.util.List;

/**
 * Zombies-mode enemy: a Zombie subclass (vanilla body, collision, gravity, pathfinding) whose behaviour is one explicit
 * state machine ({@link ZState}) run by {@link Brain}. Server-authoritative; the state is synced to clients
 * (DATA_STATE) where {@code crb.client.ZmZombieModel} poses the blocky model for it, and Unreal draws that pose.
 *
 *   SPAWN -> IDLE -> (BREACH -> TEAR ->) CHASE <-> SEARCH
 *   CHASE -> WINDUP -> STRIKE (damage only here) -> RECOVER -> CHASE
 *   any living state -> STAGGER (cancels attacks/moves) -> back;  any -> DEATH (terminal)
 *
 * Tuning lives in {@link ZmConfig}. Never saved, never burns, never converts, no reinforcements, no loot.
 */
public final class ZmZombie extends Zombie {
    public enum ZState { SPAWN, IDLE, SEARCH, CHASE, BREACH, TEAR, WINDUP, STRIKE, RECOVER, STAGGER, DEATH }

    /** Movement style (BO2-like variety): walker shamble, hunched runner, full sprinter, crawler (legs lost). */
    public enum Variant { WALKER, RUNNER, SPRINTER, CRAWLER }

    private static final EntityDataAccessor<Byte> DATA_STATE = SynchedEntityData.defineId(ZmZombie.class, EntityDataSerializers.BYTE);
    private static final EntityDataAccessor<Byte> DATA_VARIANT = SynchedEntityData.defineId(ZmZombie.class, EntityDataSerializers.BYTE);
    private static final EntityDataAccessor<Byte> DATA_SKIN = SynchedEntityData.defineId(ZmZombie.class, EntityDataSerializers.BYTE);
    private static final EntityDataAccessor<Byte> DATA_ATTACK = SynchedEntityData.defineId(ZmZombie.class, EntityDataSerializers.BYTE);

    public int window = -1;          // index into ZombiesGame.windows, -1 = none (map mode)
    public boolean inside;           // past the barricade
    public boolean headshot;         // last bullet hit was a headshot (Guns++ gz_headshot tag)
    public int tearCooldown, stuckTicks;
    public Vec3 lastPos = Vec3.ZERO;
    public int anim;                 // ZState ordinal (exported to Unreal)

    // Brain state (server)
    ZState state = ZState.SPAWN;
    int stateTicks, attackCooldown, noSightTicks, progressTicks;
    public int strikesLanded, transitions;
    ZState resumeAfterStagger = ZState.CHASE;
    Vec3 lastKnown, progressPos = Vec3.ZERO;
    public final EnumSet<ZState> visited = EnumSet.of(ZState.SPAWN);

    // Client-side animation bookkeeping (see crb.client.ZmZombieModel)
    public ZState clientState = ZState.SPAWN;
    public int clientStateStart;

    public ZmZombie(EntityType<? extends Zombie> type, Level level) {
        super(type, level);
        setPersistenceRequired();
    }

    @Override protected void defineSynchedData() {
        super.defineSynchedData();
        this.entityData.define(DATA_STATE, (byte) 0);
        this.entityData.define(DATA_VARIANT, (byte) 0);
        this.entityData.define(DATA_SKIN, (byte) 0);
        this.entityData.define(DATA_ATTACK, (byte) 0);
    }

    public Variant variant() { int i = entityData.get(DATA_VARIANT); return Variant.values()[Math.max(0, Math.min(3, i))]; }
    public int skin() { return entityData.get(DATA_SKIN); }
    /** Attack style of the current/last attack: 0 two-arm overhead, 1 right swipe, 2 left swipe, 3 crawler lunge. */
    public int attackStyle() { return entityData.get(DATA_ATTACK); }

    public void setVariant(Variant v) {
        entityData.set(DATA_VARIANT, (byte) v.ordinal());
        refreshDimensions();
        if (v == Variant.CRAWLER) getAttribute(Attributes.MOVEMENT_SPEED).setBaseValue(ZmConfig.crawlSpeed);
    }

    @Override public void onSyncedDataUpdated(EntityDataAccessor<?> key) {
        super.onSyncedDataUpdated(key);
        if (DATA_VARIANT.equals(key)) refreshDimensions();
    }

    @Override public net.minecraft.world.entity.EntityDimensions getDimensions(net.minecraft.world.entity.Pose pose) {
        return variant() == Variant.CRAWLER ? net.minecraft.world.entity.EntityDimensions.scalable(0.7f, 0.7f) : super.getDimensions(pose);
    }

    @Override protected float getStandingEyeHeight(net.minecraft.world.entity.Pose pose, net.minecraft.world.entity.EntityDimensions dims) {
        return dims.height * 0.85f;
    }

    public ZState zState() {
        int i = this.entityData.get(DATA_STATE);
        ZState[] v = ZState.values();
        return i >= 0 && i < v.length ? v[i] : ZState.IDLE;
    }

    void setState(ZState s) {
        if (state == ZState.DEATH || s == state) return;
        state = s; stateTicks = 0; transitions++; visited.add(s);
        anim = s.ordinal();
        this.entityData.set(DATA_STATE, (byte) s.ordinal());
        if (s == ZState.WINDUP) entityData.set(DATA_ATTACK, (byte) (variant() == Variant.CRAWLER ? 3 : random.nextInt(3)));
        if (s == ZState.WINDUP || s == ZState.STAGGER || s == ZState.TEAR || s == ZState.DEATH || s == ZState.SPAWN) getNavigation().stop();
    }

    @Override protected void registerGoals() {
        this.goalSelector.addGoal(0, new FloatGoal(this));
        this.goalSelector.addGoal(1, new Brain());
    }

    @Override protected void addBehaviourGoals() { }
    @Override protected boolean isSunSensitive() { return false; }
    @Override protected boolean convertsInWater() { return false; }
    @Override public boolean canBreakDoors() { return false; }
    @Override public boolean shouldBeSaved() { return false; }
    @Override public boolean removeWhenFarAway(double d) { return false; }
    @Override protected boolean shouldDropLoot() { return false; }
    @Override public boolean isBaby() { return false; }
    @Override protected boolean shouldDespawnInPeaceful() { return false; }

    @Override public void tick() {
        super.tick();
        if (level().isClientSide) {
            ZState s = zState();
            if (s != clientState) { clientState = s; clientStateStart = tickCount; }
        } else if (isAlive() && state != ZState.DEATH) {
            separate();
        }
    }

    /** Crowd spacing: nudge apart from other zombies inside the separation radius (keeps pressure, avoids stacking). */
    void separate() {
        double r = ZmConfig.separationRadius;
        List<ZmZombie> near = level().getEntitiesOfClass(ZmZombie.class, getBoundingBox().inflate(r), z -> z != this && z.isAlive());
        double px = 0, pz = 0;
        for (ZmZombie z : near) {
            double dx = getX() - z.getX(), dz = getZ() - z.getZ(), d = Math.sqrt(dx * dx + dz * dz);
            if (d < 1e-3) { dx = random.nextDouble() - 0.5; dz = random.nextDouble() - 0.5; d = 0.5; }
            if (d < r) { double f = (r - d) / r; px += dx / d * f; pz += dz / d * f; }
        }
        if (px != 0 || pz != 0) setDeltaMovement(getDeltaMovement().add(px * ZmConfig.separationPush, 0, pz * ZmConfig.separationPush));
    }

    @Override public boolean hurt(DamageSource src, float amount) {
        boolean r = super.hurt(src, amount);
        // A heavy hit that leaves it alive can take its legs (BO2-style crawler): smaller, slower, still deadly.
        if (r && isAlive() && !level().isClientSide && variant() != Variant.CRAWLER && amount >= getMaxHealth() * ZmConfig.crawlerHitFraction
            && random.nextFloat() < ZmConfig.crawlerChance) setVariant(Variant.CRAWLER);
        if (r && isAlive() && !level().isClientSide && amount >= ZmConfig.staggerMinDamage && state != ZState.SPAWN && state != ZState.DEATH) {
            if (state != ZState.STAGGER) resumeAfterStagger = inside || window < 0 ? ZState.CHASE : ZState.BREACH;
            if (state == ZState.STAGGER) { stateTicks = 0; } // re-hit restarts the flinch
            else setState(ZState.STAGGER);
        }
        return r;
    }

    @Override public void die(DamageSource src) {
        if (!level().isClientSide) { getNavigation().stop(); setTarget(null); setState(ZState.DEATH); }
        super.die(src);
    }

    /** Vanilla melee hook: only the Brain's STRIKE frames may land a hit (never while dead / staggered / winding up). */
    @Override public boolean doHurtTarget(net.minecraft.world.entity.Entity target) {
        if (state != ZState.STRIKE || !isAlive()) return false;
        return super.doHurtTarget(target);
    }

    boolean validTarget(LivingEntity t) {
        if (!(t instanceof Player p) || !p.isAlive() || p.isSpectator() || p.isCreative()) return false;
        return p.level() == level() && distanceToSqr(p) < ZmConfig.detectRange * ZmConfig.detectRange;
    }

    Player findTarget() {
        Player best = null; double bd = Double.MAX_VALUE;
        for (Player p : level().players()) {
            if (!validTarget(p)) continue;
            double d = distanceToSqr(p);
            if (d < bd) { bd = d; best = p; }
        }
        return best;
    }

    double horizontalDist(LivingEntity t) { double dx = t.getX() - getX(), dz = t.getZ() - getZ(); return Math.sqrt(dx * dx + dz * dz); }

    /** The whole behaviour: one goal holding MOVE/LOOK/JUMP so nothing else can fight it for the body. */
    final class Brain extends Goal {
        Brain() { setFlags(EnumSet.of(Flag.MOVE, Flag.LOOK, Flag.JUMP)); }
        @Override public boolean canUse() { return isAlive(); }
        @Override public boolean canContinueToUse() { return isAlive(); }
        @Override public boolean requiresUpdateEveryTick() { return true; }

        @Override public void tick() {
            stateTicks++;
            if (attackCooldown > 0) attackCooldown--;
            LivingEntity target = getTarget();
            if (target != null && !validTarget(target)) { setTarget(null); target = null; }
            switch (state) {
                case SPAWN -> { if (stateTicks >= 12) setState(ZState.IDLE); }
                case IDLE -> idle();
                case BREACH, TEAR -> breach();
                case CHASE -> chase(target);
                case SEARCH -> search(target);
                case WINDUP -> {
                    if (target != null) getLookControl().setLookAt(target, 60, 60);
                    if (stateTicks >= ZmConfig.windupTicks) { setState(ZState.STRIKE); strike(target); }
                }
                case STRIKE -> { if (stateTicks >= ZmConfig.activeTicks) setState(ZState.RECOVER); }
                case RECOVER -> { if (stateTicks >= ZmConfig.recoverTicks) { attackCooldown = ZmConfig.attackCooldownTicks; setState(ZState.CHASE); } }
                case STAGGER -> {
                    if (stateTicks == 1 && target != null) {
                        Vec3 away = position().subtract(target.position()).multiply(1, 0, 1);
                        if (away.lengthSqr() > 1e-4) { away = away.normalize().scale(ZmConfig.knockback); setDeltaMovement(getDeltaMovement().add(away.x, 0.08, away.z)); }
                    }
                    if (stateTicks >= ZmConfig.staggerTicks) setState(resumeAfterStagger);
                }
                case DEATH -> getNavigation().stop();
            }
        }

        void idle() {
            ZombiesGame.Window w = ZombiesGame.INSTANCE.window(window);
            if (!inside && w != null) { setState(ZState.BREACH); return; }
            Player p = findTarget();
            if (p != null) { setTarget(p); if (stateTicks >= ZmConfig.reactionTicks) setState(ZState.CHASE); }
        }

        void breach() {
            ZombiesGame.Window w = ZombiesGame.INSTANCE.window(window);
            if (w == null || inside || ZombiesGame.INSTANCE.isInside(position())) { inside = true; setState(ZState.CHASE); return; }
            if (stateTicks > ZmConfig.breachGiveUpTicks) { Vec3 s = w.spawn(); teleportTo(s.x, s.y, s.z); state = ZState.IDLE; setState(ZState.BREACH); return; }
            Vec3 approach = w.approach(), gap = w.gapCenter(), in = w.insidePoint();
            if (w.boards > 0 && position().distanceTo(approach) > 1.3) {
                if (state == ZState.TEAR) setState(ZState.BREACH);
                if (stateTicks % ZmConfig.repathTicks == 1 || getNavigation().isDone()) getNavigation().moveTo(approach.x, approach.y, approach.z, 1.0);
                return;
            }
            getLookControl().setLookAt(gap.x, gap.y, gap.z);
            if (w.boards > 0) {
                if (state != ZState.TEAR) { setState(ZState.TEAR); tearCooldown = ZmConfig.tearTicks / 2; }
                if (--tearCooldown <= 0) {
                    tearCooldown = ZmConfig.tearTicks;
                    swing(InteractionHand.MAIN_HAND);
                    ZombiesGame.INSTANCE.tearBoard(w, ZmZombie.this);
                }
            } else {
                if (state == ZState.TEAR) setState(ZState.BREACH);
                if (stateTicks % ZmConfig.repathTicks == 1 || getNavigation().isDone()) getNavigation().moveTo(in.x, in.y, in.z, 1.0);
            }
        }

        void chase(LivingEntity target) {
            if (target == null) {
                Player p = findTarget();
                if (p == null) { setState(lastKnown != null ? ZState.SEARCH : ZState.IDLE); return; }
                setTarget(p); target = p;
            }
            boolean sees = getSensing().hasLineOfSight(target);
            if (sees) { noSightTicks = 0; lastKnown = target.position(); }
            else if (++noSightTicks > ZmConfig.loseSightTicks) { setTarget(null); setState(ZState.SEARCH); return; }
            getLookControl().setLookAt(target, 30, 30);
            double d = horizontalDist(target);
            if (d <= ZmConfig.attackReach && Math.abs(target.getY() - getY()) < 2.0 && sees && attackCooldown <= 0) { setState(ZState.WINDUP); return; }
            double speed = d < ZmConfig.closeRange ? ZmConfig.closeBoost : 1.0;
            if (stateTicks % ZmConfig.repathTicks == 1 || getNavigation().isDone()) {
                Path path = getNavigation().createPath(target, 0);
                if (path != null) getNavigation().moveTo(path, speed); else getNavigation().moveTo(target.getX(), target.getY(), target.getZ(), speed);
            }
            unstick();
        }

        void search(LivingEntity target) {
            Player p = target instanceof Player pl ? pl : findTarget();
            if (p != null && getSensing().hasLineOfSight(p)) { setTarget(p); setState(ZState.CHASE); return; }
            if (lastKnown == null || stateTicks > ZmConfig.searchGiveUpTicks) { lastKnown = null; setState(ZState.IDLE); return; }
            if (stateTicks % ZmConfig.repathTicks == 1) getNavigation().moveTo(lastKnown.x, lastKnown.y, lastKnown.z, 0.9);
            if (position().distanceToSqr(lastKnown) < 2.0) {
                // At the last known spot: look around; the nearest valid player (the horde always knows) resumes the chase.
                getLookControl().setLookAt(lastKnown.x + Math.sin(stateTicks * 0.1) * 4, lastKnown.y + 1.5, lastKnown.z + Math.cos(stateTicks * 0.1) * 4);
                if (p != null && stateTicks > 30) { setTarget(p); setState(ZState.CHASE); }
            }
        }

        void strike(LivingEntity target) {
            swing(InteractionHand.MAIN_HAND);
            if (target != null && isAlive() && horizontalDist(target) <= ZmConfig.attackReach + 0.4 && Math.abs(target.getY() - getY()) < 2.0) {
                if (doHurtTarget(target)) strikesLanded++;
            }
        }

        /** Same spot for stuckTicks while chasing: drop the path and sidestep so the next path differs. */
        void unstick() {
            if (position().distanceToSqr(progressPos) > 0.25) { progressPos = position(); progressTicks = 0; return; }
            if (++progressTicks < ZmConfig.stuckTicks) return;
            progressTicks = 0;
            getNavigation().stop();
            double a = random.nextDouble() * Math.PI * 2;
            getMoveControl().setWantedPosition(getX() + Math.cos(a) * 2, getY(), getZ() + Math.sin(a) * 2, 1.0);
            getJumpControl().jump();
        }
    }

    /** Applied by ZombiesGame at spawn: walker / runner / sprinter speed and the configured combat values. */
    public void applyConfig(double speed) {
        entityData.set(DATA_SKIN, (byte) random.nextInt(3));
        entityData.set(DATA_VARIANT, (byte) (speed >= ZmConfig.sprintSpeed ? Variant.SPRINTER : speed >= ZmConfig.runSpeed ? Variant.RUNNER : Variant.WALKER).ordinal());
        getAttribute(Attributes.MOVEMENT_SPEED).setBaseValue(speed);
        getAttribute(Attributes.ATTACK_DAMAGE).setBaseValue(ZmConfig.attackDamage);
        getAttribute(Attributes.FOLLOW_RANGE).setBaseValue(ZmConfig.detectRange);
        getAttribute(Attributes.SPAWN_REINFORCEMENTS_CHANCE).setBaseValue(0);
    }
}
