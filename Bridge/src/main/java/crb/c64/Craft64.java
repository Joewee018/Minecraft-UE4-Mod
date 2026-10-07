package crb.c64;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.core.BlockPos;
import net.minecraft.core.particles.DustParticleOptions;
import net.minecraft.core.particles.ParticleOptions;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.chat.Component;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.AttributeModifier;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.projectile.ProjectileUtil;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3f;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Craft 64: a Doom 64-style first-person shooter mode played in the open Minecraft world. Server thread owns all rules:
 * the Doom 64 arsenal (rebuilt from Minecraft materials), four ammo pools, 100 health + green/blue armor, pickups that
 * drop from kills and supply caches, weapon unlocks by kill count, no fall damage, no natural regeneration, fast movement.
 * Every hit is real Minecraft damage on real Minecraft mobs; visuals are vanilla particles and sounds (Unreal draws the
 * Doom-style weapon sprites and HUD from the exported view).
 */
public final class Craft64 {
    public static final Craft64 INSTANCE = new Craft64();
    private static final UUID SPEED_ID = UUID.fromString("6c1d0a64-64c6-4d64-8a64-c6400000c640");
    private static final UUID HEALTH_ID = UUID.fromString("6c1d0a64-64c6-4d64-8a64-c6400000c641");
    public static final String TAG = "crb_c64";
    private final Random rng = new Random();

    /** Everything Unreal shows (client thread reads it). */
    public record View(boolean on, int health, int armor, int armorType, String weapon, String pending, List<String> owned,
                       int[] ammo, int[] max, long fireSeq, String lastFired, int kills, String message, long messageSeq,
                       long hurtSeq, long bonusSeq, boolean berserk, int refire, int bfgCharge, long tick, int pickupsNear,
                       long hits, long shots) {}
    public static volatile View VIEW = new View(false, 0, 0, 0, "", "", List.of(), new int[5], new int[5], 0, "", 0, "", 0, 0, 0, false, 0, 0, 0, 0, 0, 0);

    static final class P {
        final UUID id; boolean fire; int selectSeq = -1; String want = "";
        C64Weapon weapon = C64Weapon.PISTOL; String pending = "";
        final EnumSet<C64Weapon> owned = EnumSet.of(C64Weapon.FIST, C64Weapon.PISTOL);
        final int[] ammo = new int[C64Ammo.values().length]; boolean backpack, berserk;
        int armor, armorType, cooldown, bfgCharge, kills, supplyTimer = 200, switchTicks; long lastFireTick = -100;
        long fireSeq, hurtSeq, bonusSeq, messageSeq, hits, shots; String message = "", lastFired = "";
        P(UUID id) { this.id = id; }
        int max(C64Ammo a) { return backpack ? a.max * 2 : a.max; }
    }
    private final Map<UUID, P> players = new ConcurrentHashMap<>();
    private final List<Proj> projectiles = new ArrayList<>();
    private long tick;

    static final class Proj {
        final String kind; final UUID owner; Vec3 pos, vel; int life; final int dmg;
        Proj(String kind, UUID owner, Vec3 pos, Vec3 vel, int dmg) { this.kind = kind; this.owner = owner; this.pos = pos; this.vel = vel; this.dmg = dmg; life = 200; }
    }

    // ================================================================================================= on / off
    public boolean isOn(UUID id) { return players.containsKey(id); }

    public String setEnabled(ServerPlayer p, boolean on) {
        if (on == isOn(p.getUUID())) return on ? "Craft 64 already on" : "Craft 64 already off";
        if (on) {
            P s = new P(p.getUUID());
            s.ammo[C64Ammo.BULLETS.ordinal()] = 50;
            players.put(p.getUUID(), s);
            attributes(p, true);
            p.setHealth(100f);
            msg(s, "CRAFT 64 - find weapons, kill everything");
            sound(p, SoundEvents.PLAYER_LEVELUP, 0.8f, 0.6f);
        } else {
            players.remove(p.getUUID());
            attributes(p, false);
            p.setHealth(Math.min(p.getHealth(), p.getMaxHealth()));
        }
        publish(p.getServer());
        return on ? "Craft 64 on" : "Craft 64 off";
    }

    private void attributes(ServerPlayer p, boolean on) {
        AttributeInstance hp = p.getAttribute(Attributes.MAX_HEALTH), sp = p.getAttribute(Attributes.MOVEMENT_SPEED);
        if (hp != null) { hp.removeModifier(HEALTH_ID); if (on) hp.addTransientModifier(new AttributeModifier(HEALTH_ID, "craft64 health", 180, AttributeModifier.Operation.ADDITION)); }
        if (sp != null) { sp.removeModifier(SPEED_ID); if (on) sp.addTransientModifier(new AttributeModifier(SPEED_ID, "craft64 speed", 0.35, AttributeModifier.Operation.MULTIPLY_BASE)); }
        if (!on && p.getHealth() > p.getMaxHealth()) p.setHealth(p.getMaxHealth());
    }

    /** Client thread -> server thread: fire held, weapon selection (key + press counter). */
    public void input(UUID id, boolean fire, String want, int selectSeq) {
        P s = players.get(id); if (s == null) return;
        s.fire = fire;
        if (selectSeq < 0) return;                       // stale input: release the trigger only
        if (selectSeq != s.selectSeq) { if (s.selectSeq >= 0 && want != null && !want.isEmpty()) s.want = want; s.selectSeq = selectSeq; }
    }

    // ================================================================================================= tick
    public void tick(MinecraftServer server) {
        tick++;
        if (players.isEmpty() && projectiles.isEmpty()) { if (VIEW.on()) publish(server); return; }
        for (P s : players.values()) {
            ServerPlayer p = server.getPlayerList().getPlayer(s.id);
            if (p == null) continue;
            tickPlayer(p, s);
        }
        tickProjectiles(server);
        publish(server);
    }

    private void tickPlayer(ServerPlayer p, P s) {
        // Doom rules: no regeneration (food held just under the regen threshold, never starving), no fall damage.
        p.getFoodData().setFoodLevel(17); p.getFoodData().setSaturation(0);
        p.resetFallDistance();
        if (p.isDeadOrDying()) return;
        // weapon switching (Doom: lower, then raise)
        if (!s.want.isEmpty()) {
            C64Weapon w = resolveSelect(s, s.want); s.want = "";
            if (w != null && w != s.weapon) { s.pending = w.key; s.switchTicks = 6; }
        }
        if (s.switchTicks > 0 && --s.switchTicks == 0 && !s.pending.isEmpty()) { s.weapon = C64Weapon.byKey(s.pending); s.pending = ""; s.cooldown = 4; }
        if (s.cooldown > 0) s.cooldown--;
        // BFG: hold to charge (Doom: 30 tics before the ball leaves)
        if (s.weapon == C64Weapon.BFG && s.bfgCharge > 0) {
            if (++s.bfgCharge >= 17) { s.bfgCharge = 0; fireNow(p, s, C64Weapon.BFG); }
        } else if (s.fire && s.cooldown == 0 && s.switchTicks == 0) {
            C64Weapon w = s.weapon;
            if (w.ammo != C64Ammo.NONE && s.ammo[w.ammo.ordinal()] < w.use) { autoSwitch(p, s); }
            else if (w == C64Weapon.BFG) { s.bfgCharge = 1; s.cooldown = w.refire; sound(p, SoundEvents.BEACON_POWER_SELECT, 1f, 1.4f); }
            else fireNow(p, s, w);
        }
        pickups(p, s);
        if (--s.supplyTimer <= 0) { s.supplyTimer = 600; supplyDrop(p, s); }
    }

    private C64Weapon resolveSelect(P s, String want) {
        if (want.equals("next") || want.equals("prev")) {
            List<C64Weapon> list = new ArrayList<>(s.owned);
            int i = list.indexOf(s.weapon);
            for (int k = 1; k <= list.size(); k++) {
                C64Weapon w = list.get(Math.floorMod(i + (want.equals("next") ? k : -k), list.size()));
                if (w.ammo == C64Ammo.NONE || s.ammo[w.ammo.ordinal()] >= w.use) return w;
            }
            return null;
        }
        try {
            int slot = Integer.parseInt(want);
            // Doom: pressing a shared slot again toggles (1: fist / chainsaw, 3: shotgun / super shotgun)
            List<C64Weapon> inSlot = new ArrayList<>(); for (C64Weapon w : s.owned) if (w.slot == slot) inSlot.add(w);
            if (inSlot.isEmpty()) return null;
            int i = inSlot.indexOf(s.weapon);
            return inSlot.get(i < 0 ? inSlot.size() - 1 : (i + 1) % inSlot.size());
        } catch (NumberFormatException ex) {
            C64Weapon w = C64Weapon.byKey(want);
            return w != null && s.owned.contains(w) ? w : null;
        }
    }

    private void autoSwitch(ServerPlayer p, P s) {
        C64Weapon[] order = { C64Weapon.PLASMA, C64Weapon.SUPER, C64Weapon.CHAINGUN, C64Weapon.SHOTGUN, C64Weapon.PISTOL, C64Weapon.CHAINSAW, C64Weapon.UNMAKER, C64Weapon.ROCKET, C64Weapon.BFG, C64Weapon.FIST };
        for (C64Weapon w : order)
            if (s.owned.contains(w) && (w.ammo == C64Ammo.NONE || s.ammo[w.ammo.ordinal()] >= w.use) && w != s.weapon) { s.pending = w.key; s.switchTicks = 6; return; }
    }

    // ================================================================================================= firing
    private void fireNow(ServerPlayer p, P s, C64Weapon w) {
        ServerLevel l = p.serverLevel();
        if (w.ammo != C64Ammo.NONE) s.ammo[w.ammo.ordinal()] = Math.max(0, s.ammo[w.ammo.ordinal()] - w.use);
        boolean rested = tick - s.lastFireTick > w.refire + 3;   // Doom: the first pistol / chaingun shot of a burst is dead accurate
        s.cooldown = w.refire; s.fireSeq++; s.shots++; s.lastFired = w.key; s.lastFireTick = tick;
        Vec3 eye = p.getEyePosition(), look = aim(p, p.getLookAngle(), w.range);
        switch (w.hit) {
            case MELEE -> {
                int dmg = w.roll(rng) * (s.berserk && w == C64Weapon.FIST ? 10 : 1);
                LivingEntity t = firstLiving(p, eye, eye.add(look.scale(w.range)));
                if (t != null) {
                    hurt(p, s, t, dmg, l.damageSources().playerAttack(p));
                    puff(l, t.position().add(0, t.getBbHeight() * 0.6, 0), true);
                }
                sound(p, w == C64Weapon.CHAINSAW ? SoundEvents.GRINDSTONE_USE : SoundEvents.PLAYER_ATTACK_STRONG, w == C64Weapon.CHAINSAW ? 0.7f : 0.9f, t == null ? 1.3f : 1f);
            }
            case SCAN -> {
                boolean accurate = rested && (w == C64Weapon.PISTOL || w == C64Weapon.CHAINGUN);
                for (int i = 0; i < w.pellets; i++) {
                    Vec3 d = spread(look, accurate ? 0 : w.spreadH, w.spreadV);
                    scan(p, s, eye, d, w.range, w.roll(rng));
                }
                SoundEvent snd = w == C64Weapon.PISTOL || w == C64Weapon.CHAINGUN ? SoundEvents.FIREWORK_ROCKET_BLAST : SoundEvents.GENERIC_EXPLODE;
                sound(p, snd, w == C64Weapon.SUPER ? 0.9f : w == C64Weapon.SHOTGUN ? 0.6f : 0.9f, w == C64Weapon.PISTOL ? 1.7f : w == C64Weapon.CHAINGUN ? 1.4f : w == C64Weapon.SHOTGUN ? 1.9f : 1.5f);
            }
            case PROJ -> {
                Vec3 start = eye.add(look.scale(0.6)).add(0, -0.15, 0);
                projectiles.add(new Proj(w.projectile, p.getUUID(), start, look.scale(w.speed), w.roll(rng)));
                sound(p, w == C64Weapon.ROCKET ? SoundEvents.FIREWORK_ROCKET_LAUNCH : w == C64Weapon.PLASMA ? SoundEvents.BLAZE_SHOOT : SoundEvents.WARDEN_SONIC_BOOM,
                      w == C64Weapon.BFG ? 0.8f : 0.9f, w == C64Weapon.PLASMA ? 1.8f : 1f);
            }
            case BEAM -> {
                Vec3 end = eye.add(look.scale(w.range));
                BlockHitResult bh = l.clip(new ClipContext(eye, end, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, p));
                if (bh.getType() != HitResult.Type.MISS) end = bh.getLocation();
                // the Unmaker's laser pierces every enemy on the line
                for (LivingEntity t : l.getEntitiesOfClass(LivingEntity.class, new AABB(eye, end).inflate(1), e -> e != p && e.isAlive()))
                    if (t.getBoundingBox().inflate(0.2).clip(eye, end).isPresent()) hurt(p, s, t, w.roll(rng), l.damageSources().indirectMagic(p, p));
                line(l, eye.add(0, -0.2, 0), end, new DustParticleOptions(new Vector3f(1f, 0.1f, 0.1f), 1.4f), 0.5);
                sound(p, SoundEvents.BEACON_DEACTIVATE, 0.7f, 2f);
            }
        }
    }

    /** Doom-style vertical autoaim: a living enemy within a few degrees of the crosshair pulls the shot onto it. */
    private Vec3 aim(ServerPlayer p, Vec3 look, double range) {
        Vec3 eye = p.getEyePosition();
        LivingEntity best = null; double bestA = Math.toRadians(4.5);
        for (LivingEntity e : p.serverLevel().getEntitiesOfClass(LivingEntity.class, p.getBoundingBox().inflate(Math.min(range, 48)), e -> e != p && e.isAlive() && e instanceof Enemy)) {
            Vec3 to = e.position().add(0, e.getBbHeight() * 0.55, 0).subtract(eye);
            double a = Math.acos(Math.max(-1, Math.min(1, to.normalize().dot(look))));
            if (a < bestA && p.hasLineOfSight(e)) { bestA = a; best = e; }
        }
        return best == null ? look : best.position().add(0, best.getBbHeight() * 0.55, 0).subtract(eye).normalize();
    }

    private Vec3 spread(Vec3 d, double h, double v) {
        if (h <= 0 && v <= 0) return d;
        double yaw = Math.atan2(-d.x, d.z) + Math.toRadians((rng.nextDouble() - rng.nextDouble()) * h);
        double pitch = Math.asin(Math.max(-1, Math.min(1, -d.y))) + Math.toRadians((rng.nextDouble() - rng.nextDouble()) * v);
        return new Vec3(-Math.sin(yaw) * Math.cos(pitch), -Math.sin(pitch), Math.cos(yaw) * Math.cos(pitch));
    }

    private void scan(ServerPlayer p, P s, Vec3 eye, Vec3 dir, double range, int dmg) {
        ServerLevel l = p.serverLevel();
        Vec3 end = eye.add(dir.scale(range));
        BlockHitResult bh = l.clip(new ClipContext(eye, end, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, p));
        if (bh.getType() != HitResult.Type.MISS) end = bh.getLocation();
        EntityHitResult eh = ProjectileUtil.getEntityHitResult(l, p, eye, end, new AABB(eye, end).inflate(1), e -> e instanceof LivingEntity && e.isAlive() && e != p);
        if (eh != null && eh.getEntity() instanceof LivingEntity t) { hurt(p, s, t, dmg, l.damageSources().playerAttack(p)); puff(l, eh.getLocation(), true); }
        else if (bh.getType() != HitResult.Type.MISS) puff(l, end, false);
    }

    private LivingEntity firstLiving(ServerPlayer p, Vec3 from, Vec3 to) {
        EntityHitResult eh = ProjectileUtil.getEntityHitResult(p.serverLevel(), p, from, to, new AABB(from, to).inflate(1), e -> e instanceof LivingEntity && e.isAlive() && e != p);
        return eh != null ? (LivingEntity) eh.getEntity() : null;
    }

    private void hurt(ServerPlayer p, P s, LivingEntity t, float dmg, DamageSource src) {
        t.invulnerableTime = 0;                          // Doom: every pellet / tic counts
        boolean wasAlive = t.isAlive();
        if (t.hurt(src, dmg)) s.hits++;
        if (wasAlive && t.isDeadOrDying() && t instanceof Enemy) onKill(p, s, t);
    }

    // ================================================================================================= projectiles
    private void tickProjectiles(MinecraftServer server) {
        for (Iterator<Proj> it = projectiles.iterator(); it.hasNext(); ) {
            Proj pr = it.next();
            ServerPlayer owner = server.getPlayerList().getPlayer(pr.owner);
            if (owner == null || --pr.life <= 0) { it.remove(); continue; }
            ServerLevel l = owner.serverLevel();
            Vec3 from = pr.pos, to = pr.pos.add(pr.vel);
            BlockHitResult bh = l.clip(new ClipContext(from, to, ClipContext.Block.COLLIDER, ClipContext.Fluid.NONE, owner));
            Vec3 stop = bh.getType() != HitResult.Type.MISS ? bh.getLocation() : to;
            EntityHitResult eh = ProjectileUtil.getEntityHitResult(l, owner, from, stop, new AABB(from, stop).inflate(pr.kind.equals("bfg") ? 0.8 : 0.4),
                e -> e instanceof LivingEntity && e.isAlive() && e != owner);
            trail(l, pr, from, stop);
            if (eh != null || bh.getType() != HitResult.Type.MISS) {
                Vec3 at = eh != null ? eh.getLocation() : stop;
                P s = players.get(pr.owner);
                if (s != null) impact(owner, s, pr, at, eh != null ? (LivingEntity) eh.getEntity() : null);
                it.remove();
                continue;
            }
            pr.pos = to;
        }
    }

    private void trail(ServerLevel l, Proj pr, Vec3 a, Vec3 b) {
        switch (pr.kind) {
            case "rocket" -> { line(l, a, b, ParticleTypes.SMOKE, 0.25); line(l, a, b, ParticleTypes.FLAME, 0.6); }
            case "plasma" -> line(l, a, b, ParticleTypes.SOUL_FIRE_FLAME, 0.3);
            case "bfg" -> { line(l, a, b, ParticleTypes.HAPPY_VILLAGER, 0.15); l.sendParticles(ParticleTypes.GLOW, b.x, b.y, b.z, 6, 0.25, 0.25, 0.25, 0.02); }
        }
    }

    private void impact(ServerPlayer p, P s, Proj pr, Vec3 at, LivingEntity direct) {
        ServerLevel l = p.serverLevel();
        switch (pr.kind) {
            case "rocket" -> {
                if (direct != null) hurt(p, s, direct, pr.dmg, l.damageSources().explosion(p, p));
                radius(p, s, at, 4.0, 128, direct);
                l.sendParticles(ParticleTypes.EXPLOSION, at.x, at.y, at.z, 4, 0.6, 0.6, 0.6, 0);
                l.sendParticles(ParticleTypes.FLAME, at.x, at.y, at.z, 30, 0.6, 0.6, 0.6, 0.08);
                l.playSound(null, at.x, at.y, at.z, SoundEvents.GENERIC_EXPLODE, SoundSource.PLAYERS, 1.2f, 1f);
            }
            case "plasma" -> {
                if (direct != null) hurt(p, s, direct, pr.dmg, l.damageSources().indirectMagic(p, p));
                l.sendParticles(ParticleTypes.SOUL_FIRE_FLAME, at.x, at.y, at.z, 8, 0.15, 0.15, 0.15, 0.05);
                l.playSound(null, at.x, at.y, at.z, SoundEvents.FIRE_EXTINGUISH, SoundSource.PLAYERS, 0.4f, 1.8f);
            }
            case "bfg" -> {
                if (direct != null) hurt(p, s, direct, pr.dmg, l.damageSources().indirectMagic(p, p));
                l.sendParticles(ParticleTypes.HAPPY_VILLAGER, at.x, at.y, at.z, 120, 1.5, 1.5, 1.5, 0.3);
                l.sendParticles(ParticleTypes.FLASH, at.x, at.y, at.z, 1, 0, 0, 0, 0);
                l.playSound(null, at.x, at.y, at.z, SoundEvents.GENERIC_EXPLODE, SoundSource.PLAYERS, 1.5f, 0.6f);
                // Doom BFG: 40 tracers fan out over 90 degrees from the shooter's view at the moment of impact
                Vec3 eye = p.getEyePosition(); double yaw0 = Math.toRadians(p.getYRot());
                for (int i = 0; i < 40; i++) {
                    double yaw = yaw0 - Math.PI / 4 + Math.PI / 2 * i / 39.0;
                    Vec3 d = new Vec3(-Math.sin(yaw), 0, Math.cos(yaw));
                    LivingEntity t = null; double best = 32;
                    for (LivingEntity e : l.getEntitiesOfClass(LivingEntity.class, p.getBoundingBox().inflate(32), e -> e != p && e.isAlive() && e instanceof Enemy)) {
                        Vec3 to = e.position().add(0, e.getBbHeight() * 0.5, 0).subtract(eye);
                        Vec3 flat = new Vec3(to.x, 0, to.z);
                        if (flat.length() < best && Math.acos(Math.max(-1, Math.min(1, flat.normalize().dot(d)))) < Math.toRadians(2.3) && p.hasLineOfSight(e)) { best = flat.length(); t = e; }
                    }
                    if (t != null) {
                        int dmg = 0; for (int k = 0; k < 15; k++) dmg += 1 + rng.nextInt(8);
                        hurt(p, s, t, dmg, l.damageSources().indirectMagic(p, p));
                        l.sendParticles(ParticleTypes.HAPPY_VILLAGER, t.getX(), t.getY() + t.getBbHeight() * 0.5, t.getZ(), 25, 0.4, 0.6, 0.4, 0.1);
                    }
                }
            }
        }
    }

    private void radius(ServerPlayer p, P s, Vec3 at, double r, int maxDmg, LivingEntity skip) {
        ServerLevel l = p.serverLevel();
        for (LivingEntity e : l.getEntitiesOfClass(LivingEntity.class, new AABB(at, at).inflate(r), e -> e.isAlive() && e != skip)) {
            double d = Math.max(0, e.position().add(0, e.getBbHeight() * 0.5, 0).distanceTo(at) - e.getBbWidth() * 0.5);
            if (d >= r) continue;
            float dmg = (float) (maxDmg * (1 - d / r));
            if (e == p) { e.invulnerableTime = 0; e.hurt(l.damageSources().explosion(p, p), dmg * 0.5f); }   // Doom: rockets hurt you too
            else hurt(p, s, e, dmg, l.damageSources().explosion(p, p));
        }
    }

    // ================================================================================================= kills, drops, pickups
    private static final int[][] UNLOCKS = {
        { C64Weapon.SHOTGUN.ordinal(), 2 }, { C64Weapon.CHAINSAW.ordinal(), 4 }, { C64Weapon.CHAINGUN.ordinal(), 7 },
        { C64Weapon.SUPER.ordinal(), 12 }, { C64Weapon.ROCKET.ordinal(), 18 }, { C64Weapon.PLASMA.ordinal(), 26 },
        { C64Weapon.UNMAKER.ordinal(), 36 }, { C64Weapon.BFG.ordinal(), 48 } };

    private void onKill(ServerPlayer p, P s, LivingEntity t) {
        s.kills++;
        ServerLevel l = p.serverLevel();
        Vec3 at = t.position().add(0, 0.3, 0);
        for (int[] u : UNLOCKS) {
            C64Weapon w = C64Weapon.values()[u[0]];
            if (s.kills >= u[1] && !s.owned.contains(w) && !droppedWeapon.contains(s.id + w.key)) { droppedWeapon.add(s.id + w.key); drop(l, at, "weapon", w.key, 1); return; }
        }
        String type = net.minecraft.core.registries.BuiltInRegistries.ENTITY_TYPE.getKey(t.getType()).getPath();
        String ammo = switch (type) {
            case "skeleton", "stray", "pillager" -> "shells";
            case "creeper", "ravager" -> "rockets";
            case "enderman", "witch", "blaze", "phantom", "evoker", "vindicator" -> "cells";
            default -> "bullets";
        };
        if (rng.nextInt(100) < 75) drop(l, at, "ammo", ammo, 1);
        int r = rng.nextInt(100);
        if (r < 14) drop(l, at, "health", "stimpack", 1);
        else if (r < 18) drop(l, at, "health", "medikit", 1);
        else if (r < 26) drop(l, at, "armor", "bonus", 1);
        else if (r < 34) drop(l, at, "health", "bonus", 1);
        else if (r < 35) drop(l, at, "health", "soulsphere", 1);
    }
    private final Set<String> droppedWeapon = ConcurrentHashMap.newKeySet();

    /** Supply cache near the player every 30 s: ammo for what they carry, sometimes health / armor / power-ups. */
    private void supplyDrop(ServerPlayer p, P s) {
        ServerLevel l = p.serverLevel();
        if (countNear(p, 32) >= 6) return;
        for (int n = 0; n < 2; n++) {
            double a = rng.nextDouble() * Math.PI * 2, d = 6 + rng.nextDouble() * 12;
            int x = (int) Math.floor(p.getX() + Math.cos(a) * d), z = (int) Math.floor(p.getZ() + Math.sin(a) * d);
            int y = l.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, x, z);
            if (Math.abs(y - p.getY()) > 12) continue;
            Vec3 at = new Vec3(x + 0.5, y + 0.2, z + 0.5);
            int r = rng.nextInt(100);
            List<C64Ammo> carried = new ArrayList<>(); for (C64Weapon w : s.owned) if (w.ammo != C64Ammo.NONE && !carried.contains(w.ammo)) carried.add(w.ammo);
            if (r < 55 && !carried.isEmpty()) drop(l, at, "ammo", carried.get(rng.nextInt(carried.size())).key, 2);
            else if (r < 70) drop(l, at, "health", "medikit", 1);
            else if (r < 82) drop(l, at, "armor", rng.nextInt(4) == 0 ? "blue" : "green", 1);
            else if (r < 88) drop(l, at, "power", "berserk", 1);
            else if (r < 92) drop(l, at, "power", "backpack", 1);
            else if (r < 95) drop(l, at, "health", "soulsphere", 1);
            else drop(l, at, "health", "stimpack", 2);
            l.sendParticles(ParticleTypes.END_ROD, at.x, at.y + 0.5, at.z, 12, 0.2, 0.6, 0.2, 0.02);
        }
    }

    private int countNear(ServerPlayer p, double r) {
        return p.serverLevel().getEntitiesOfClass(ItemEntity.class, p.getBoundingBox().inflate(r), e -> e.getItem().getTag() != null && e.getItem().getTag().contains(TAG)).size();
    }

    static Item itemFor(String kind, String what) {
        return switch (kind + ":" + what) {
            case "ammo:bullets" -> Items.IRON_NUGGET;
            case "ammo:shells" -> Items.GOLD_NUGGET;
            case "ammo:rockets" -> Items.FIREWORK_ROCKET;
            case "ammo:cells" -> Items.GLOWSTONE_DUST;
            case "health:stimpack" -> Items.GLISTERING_MELON_SLICE;
            case "health:medikit" -> Items.GOLDEN_APPLE;
            case "health:bonus" -> Items.SWEET_BERRIES;
            case "health:soulsphere" -> Items.HEART_OF_THE_SEA;
            case "armor:green" -> Items.IRON_CHESTPLATE;
            case "armor:blue" -> Items.DIAMOND_CHESTPLATE;
            case "armor:bonus" -> Items.SCUTE;
            case "power:berserk" -> Items.BLAZE_POWDER;
            case "power:backpack" -> Items.CHEST;
            default -> C64Items.weapon(what);
        };
    }

    public void drop(ServerLevel l, Vec3 at, String kind, String what, int amount) {
        Item it = itemFor(kind, what);
        if (it == null) return;
        ItemStack st = new ItemStack(it);
        CompoundTag t = new CompoundTag(); t.putString("kind", kind); t.putString("what", what); t.putInt("amount", amount);
        st.getOrCreateTag().put(TAG, t);
        st.setHoverName(Component.literal(label(kind, what)));
        ItemEntity e = new ItemEntity(l, at.x, at.y, at.z, st, 0, 0.25, 0);
        e.setNeverPickUp(); e.setUnlimitedLifetime(); e.setGlowingTag(kind.equals("weapon"));
        l.addFreshEntity(e);
    }

    static String label(String kind, String what) {
        return switch (kind) {
            case "weapon" -> { C64Weapon w = C64Weapon.byKey(what); yield w == null ? what : w.display; }
            case "ammo" -> what.substring(0, 1).toUpperCase() + what.substring(1);
            case "armor" -> what.equals("bonus") ? "Armor Bonus" : what.equals("blue") ? "Mega Armor" : "Security Armor";
            case "health" -> switch (what) { case "stimpack" -> "Stimpack"; case "medikit" -> "Medikit"; case "soulsphere" -> "Soulsphere"; default -> "Health Bonus"; };
            default -> what.substring(0, 1).toUpperCase() + what.substring(1);
        };
    }

    private void pickups(ServerPlayer p, P s) {
        for (ItemEntity e : p.serverLevel().getEntitiesOfClass(ItemEntity.class, p.getBoundingBox().inflate(0.9, 0.6, 0.9), e -> e.isAlive())) {
            CompoundTag root = e.getItem().getTag();
            if (root == null || !root.contains(TAG)) continue;
            CompoundTag t = root.getCompound(TAG);
            if (take(p, s, t.getString("kind"), t.getString("what"), Math.max(1, t.getInt("amount")))) e.discard();
        }
    }

    /** Doom pickup rules; false when the item would be wasted (full health, full ammo...). */
    boolean take(ServerPlayer p, P s, String kind, String what, int amount) {
        switch (kind) {
            case "weapon" -> {
                C64Weapon w = C64Weapon.byKey(what); if (w == null) return false;
                boolean had = s.owned.contains(w);
                s.owned.add(w);
                if (w.ammo != C64Ammo.NONE) addAmmo(s, w.ammo, w.ammo == C64Ammo.CELLS ? 40 : w.ammo.clip * 2);
                if (!had) { s.pending = w.key; s.switchTicks = 6; }
                msg(s, "You got the " + w.display + "!"); s.bonusSeq++; sound(p, SoundEvents.PLAYER_LEVELUP, 0.7f, 1.5f);
                return true;
            }
            case "ammo" -> {
                C64Ammo a = Arrays.stream(C64Ammo.values()).filter(x -> x.key.equals(what)).findFirst().orElse(null);
                if (a == null || s.ammo[a.ordinal()] >= s.max(a)) return false;
                addAmmo(s, a, a.clip * amount);
                msg(s, switch (a) { case BULLETS -> "Picked up a clip."; case SHELLS -> "Picked up 4 shotgun shells."; case ROCKETS -> "Picked up a rocket."; default -> "Picked up an energy cell."; });
            }
            case "health" -> {
                float h = p.getHealth();
                switch (what) {
                    case "stimpack" -> { if (h >= 100) return false; p.setHealth(Math.min(100, h + 10 * amount)); msg(s, "Picked up a stimpack."); }
                    case "medikit" -> { if (h >= 100) return false; p.setHealth(Math.min(100, h + 25)); msg(s, h < 25 ? "Picked up a medikit that you REALLY need!" : "Picked up a medikit."); }
                    case "soulsphere" -> { p.setHealth(Math.min(200, h + 100)); msg(s, "Supercharge!"); }
                    default -> { p.setHealth(Math.min(200, h + 2)); msg(s, "Picked up a health bonus."); }
                }
            }
            case "armor" -> {
                switch (what) {
                    case "green" -> { if (s.armor >= 100) return false; s.armor = 100; s.armorType = 1; msg(s, "You pick up the armor."); }
                    case "blue" -> { if (s.armor >= 200) return false; s.armor = 200; s.armorType = 2; msg(s, "You got the MegaArmor!"); }
                    default -> { s.armor = Math.min(200, s.armor + 2); if (s.armorType == 0) s.armorType = 1; msg(s, "Picked up an armor bonus."); }
                }
            }
            case "power" -> {
                if (what.equals("berserk")) { s.berserk = true; p.setHealth(Math.max(p.getHealth(), 100)); s.pending = C64Weapon.FIST.key; s.switchTicks = 6; msg(s, "Berserk!"); }
                else { s.backpack = true; for (C64Ammo a : C64Ammo.values()) if (a != C64Ammo.NONE) addAmmo(s, a, a.clip); msg(s, "Picked up a backpack full of ammo!"); }
            }
            default -> { return false; }
        }
        s.bonusSeq++;
        sound(p, SoundEvents.ITEM_PICKUP, 0.6f, 1.2f);
        return true;
    }

    private void addAmmo(P s, C64Ammo a, int n) { if (a != C64Ammo.NONE) s.ammo[a.ordinal()] = Math.min(s.max(a), s.ammo[a.ordinal()] + n); }

    // ================================================================================================= damage to the player
    /** Player.actuallyHurt hook: Doom-scale incoming damage (100 HP world), then green / blue armor absorbs 1/3 or 1/2. */
    public float playerDamage(ServerPlayer p, DamageSource src, float amount) {
        P s = players.get(p.getUUID());
        if (s == null || amount <= 0) return amount;
        if (src.is(net.minecraft.tags.DamageTypeTags.IS_FALL)) return 0;
        boolean self = src.getEntity() == p;
        float a = self ? amount : Math.min(amount * 3f, amount + 60f);
        if (s.armor > 0 && s.armorType > 0) {
            int saved = (int) (a / (s.armorType == 1 ? 3 : 2));
            if (saved > s.armor) saved = s.armor;
            s.armor -= saved; if (s.armor == 0) s.armorType = 0;
            a -= saved;
        }
        s.hurtSeq++;
        return a;
    }

    public void onPlayerDeath(ServerPlayer p) {
        P s = players.get(p.getUUID()); if (s == null) return;
        P fresh = new P(p.getUUID()); fresh.ammo[C64Ammo.BULLETS.ordinal()] = 50; fresh.kills = s.kills;
        players.put(p.getUUID(), fresh);
    }

    public void onRespawn(ServerPlayer p) { if (isOn(p.getUUID())) { attributes(p, true); p.setHealth(100f); } }

    // ================================================================================================= ops (tests / debug)
    public JsonObject op(ServerPlayer p, String op, JsonObject args) {
        JsonObject r = new JsonObject();
        P s = players.get(p.getUUID());
        switch (op) {
            case "c64.on" -> r.addProperty("message", setEnabled(p, true));
            case "c64.off" -> r.addProperty("message", setEnabled(p, false));
            case "c64.give" -> {
                if (s == null) { r.addProperty("ok", false); r.addProperty("message", "Craft 64 is off"); return r; }
                String what = args.has("what") ? args.get("what").getAsString() : "all";
                if (what.equals("all")) { s.owned.addAll(EnumSet.allOf(C64Weapon.class)); s.backpack = true; for (C64Ammo a : C64Ammo.values()) if (a != C64Ammo.NONE) s.ammo[a.ordinal()] = s.max(a); s.armor = 200; s.armorType = 2; msg(s, "Very Happy Ammo Added"); }
                else { C64Weapon w = C64Weapon.byKey(what); if (w != null) take(p, s, "weapon", w.key, 1); }
                r.addProperty("message", "given " + what);
            }
            case "c64.select" -> { if (s != null && args.has("weapon")) { C64Weapon w = C64Weapon.byKey(args.get("weapon").getAsString()); if (w != null && s.owned.contains(w)) { s.weapon = w; s.pending = ""; s.switchTicks = 0; s.cooldown = 0; } } r.addProperty("message", "selected"); }
            case "c64.drop" -> {
                Vec3 at = p.position().add(p.getLookAngle().multiply(1, 0, 1).normalize().scale(args.has("dist") ? args.get("dist").getAsDouble() : 2.5)).add(0, 0.2, 0);
                drop(p.serverLevel(), at, args.get("kind").getAsString(), args.get("what").getAsString(), args.has("amount") ? args.get("amount").getAsInt() : 1);
                r.addProperty("message", "dropped");
            }
            case "c64.hurt" -> {
                float amt = args.has("amount") ? args.get("amount").getAsFloat() : 10f;
                p.invulnerableTime = 0;
                p.hurt(p.damageSources().generic(), amt);
                r.addProperty("health", p.getHealth()); r.addProperty("armor", s == null ? 0 : s.armor);
                r.addProperty("message", "hurt " + amt);
            }
            case "c64.status" -> r.addProperty("message", s == null ? "off" : "on " + s.weapon.key);
            default -> { r.addProperty("ok", false); r.addProperty("message", "unknown op " + op); return r; }
        }
        r.addProperty("ok", true);
        return r;
    }

    // ================================================================================================= helpers
    private void msg(P s, String m) { s.message = m; s.messageSeq++; }
    private void sound(ServerPlayer p, SoundEvent e, float vol, float pitch) { p.serverLevel().playSound(null, p.getX(), p.getY(), p.getZ(), e, SoundSource.PLAYERS, vol, pitch); }

    private void puff(ServerLevel l, Vec3 at, boolean blood) {
        if (blood) l.sendParticles(new DustParticleOptions(new Vector3f(0.55f, 0f, 0f), 1.2f), at.x, at.y, at.z, 6, 0.12, 0.12, 0.12, 0);
        else l.sendParticles(ParticleTypes.SMOKE, at.x, at.y, at.z, 3, 0.05, 0.05, 0.05, 0.01);
        l.sendParticles(ParticleTypes.CRIT, at.x, at.y, at.z, 2, 0.05, 0.05, 0.05, 0.1);
    }

    private void line(ServerLevel l, Vec3 a, Vec3 b, ParticleOptions o, double step) {
        Vec3 d = b.subtract(a); double len = d.length(); if (len < 1e-3) return;
        Vec3 u = d.scale(1 / len);
        for (double t = 0; t <= len; t += step) l.sendParticles(o, a.x + u.x * t, a.y + u.y * t, a.z + u.z * t, 1, 0, 0, 0, 0);
    }

    private void publish(MinecraftServer server) {
        // single player: publish the first enabled player's view
        P s = players.values().stream().findFirst().orElse(null);
        if (s == null) { if (VIEW.on()) VIEW = new View(false, 0, 0, 0, "", "", List.of(), new int[5], new int[5], 0, "", 0, "", 0, 0, 0, false, 0, 0, tick, 0, 0, 0); return; }
        ServerPlayer p = server.getPlayerList().getPlayer(s.id);
        List<String> owned = new ArrayList<>(); for (C64Weapon w : s.owned) owned.add(w.key);
        int[] mx = new int[5]; for (C64Ammo a : C64Ammo.values()) mx[a.ordinal()] = s.max(a);
        VIEW = new View(true, p == null ? 0 : (int) Math.ceil(p.getHealth()), s.armor, s.armorType, s.weapon.key, s.pending, owned, s.ammo.clone(), mx,
            s.fireSeq, s.lastFired, s.kills, s.message, s.messageSeq, s.hurtSeq, s.bonusSeq, s.berserk, s.cooldown, s.bfgCharge, tick,
            p == null ? 0 : countNear(p, 24), s.hits, s.shots);
    }

    /** JSON for the client state export ("c64"). */
    public static JsonObject viewJson() {
        View v = VIEW; JsonObject j = new JsonObject();
        j.addProperty("on", v.on()); if (!v.on()) return j;
        j.addProperty("health", v.health()); j.addProperty("armor", v.armor()); j.addProperty("armorType", v.armorType());
        j.addProperty("weapon", v.weapon()); j.addProperty("pending", v.pending());
        JsonArray o = new JsonArray(); v.owned().forEach(o::add); j.add("owned", o);
        JsonObject am = new JsonObject(), mx = new JsonObject();
        for (C64Ammo a : C64Ammo.values()) if (a != C64Ammo.NONE) { am.addProperty(a.key, v.ammo()[a.ordinal()]); mx.addProperty(a.key, v.max()[a.ordinal()]); }
        j.add("ammo", am); j.add("max", mx);
        C64Weapon w = C64Weapon.byKey(v.weapon()); j.addProperty("ammoType", w == null ? "none" : w.ammo.key);
        j.addProperty("fireSeq", v.fireSeq()); j.addProperty("lastFired", v.lastFired()); j.addProperty("kills", v.kills());
        j.addProperty("message", v.message()); j.addProperty("messageSeq", v.messageSeq()); j.addProperty("hurtSeq", v.hurtSeq()); j.addProperty("bonusSeq", v.bonusSeq());
        j.addProperty("berserk", v.berserk()); j.addProperty("refire", v.refire()); j.addProperty("bfgCharge", v.bfgCharge()); j.addProperty("tick", v.tick());
        j.addProperty("pickupsNear", v.pickupsNear()); j.addProperty("hits", v.hits()); j.addProperty("shots", v.shots());
        return j;
    }

    public void clear() { players.clear(); projectiles.clear(); droppedWeapon.clear(); VIEW = new View(false, 0, 0, 0, "", "", List.of(), new int[5], new int[5], 0, "", 0, "", 0, 0, 0, false, 0, 0, 0, 0, 0, 0); }
}
