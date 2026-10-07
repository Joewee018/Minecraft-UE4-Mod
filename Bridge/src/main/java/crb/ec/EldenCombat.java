package crb.ec;

import com.google.gson.JsonObject;
import net.minecraft.core.particles.DustParticleOptions;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.tags.DamageTypeTags;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.MobSpawnType;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.AttributeModifier;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.ShieldItem;
import net.minecraft.world.item.enchantment.EnchantmentHelper;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3f;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Minecraft x Elden Combat (mod id MinecraftEldenCombat). Server thread owns every combat rule; Unreal only sends the
 * buttons and presents Steve, the camera, lock-on and the HUD from the exported view ("ec").
 *
 * While a player has the mod ON: the attack button no longer does a vanilla swing - light / heavy / charged attacks with
 * windup, active and recovery frames, combo chains, stamina, poise (player and mobs), guarding (shield or weapon), a
 * parry window, dodge roll / backstep with invincibility frames, guard breaks, staggers and ripostes. All damage is real
 * Minecraft damage (the held item's own attack damage and enchantments, item durability, vanilla health, vanilla death).
 * OFF: the per-player state, the movement modifier, staggers it put on mobs and its debug dummies are all removed and
 * the player is back on vanilla combat; nothing is written to the world or the player's save.
 */
public final class EldenCombat {
    public static final EldenCombat INSTANCE = new EldenCombat();
    public static final String ID = "MinecraftEldenCombat";
    public static final String NAME = "Minecraft × Elden Combat";
    public static final String DUMMY_TAG = "crb_ec_dummy";
    private static final UUID SLOW_ID = UUID.fromString("ec1d0e1d-0000-4e1d-8000-00000000ec01");

    public enum Act { IDLE, LIGHT, HEAVY, CHARGE, DODGE, BACKSTEP, BLOCK, PARRY, GUARD_BREAK, STAGGER, RIPOSTE, DEAD }

    static final class S {
        final UUID id;
        // inputs (client thread writes, server thread reads)
        volatile long inLight = -1, inHeavy = -1, inParry = -1, inDodge = -1, inLock = -1, inSwitch = -1;
        volatile boolean heavyHeld, blockHeld; volatile float fwd, strafe, camYaw;
        long seenLight = -1, seenHeavy = -1, seenParry = -1, seenDodge = -1, seenLock = -1, seenSwitch = -1;
        int bufLight, bufHeavy, bufDodge, bufParry;
        // action
        Act act = Act.IDLE; int t, len, combo, chargeTicks; boolean heavy; ECWeapon weapon = ECWeapon.FIST; double mv; float charge;
        final Set<Integer> hit = new HashSet<>(); Vec3 dodgeDir = Vec3.ZERO; int riposteTarget = -1;
        // resources
        float stamina = 100, maxStamina = 100; int staminaDelay; float poise = 40, maxPoise = 40; int poiseDelay;
        // lock-on
        int lockId = -1, lockLos;
        // events (Unreal turns sequence changes into sounds, flashes and banners)
        long hitSeq, parrySeq, blockSeq, guardBreakSeq, dodgeSeq, riposteSeq, staggerSeq, hurtSeq, swingSeq, killSeq, deathSeq, eventSeq, damageSeq, mobStaggerSeq;
        String event = ""; float lastDamage; Vec3 damageAt = Vec3.ZERO; long runes; int attacks, hits, dodges;
        boolean infinite, hitboxes, ring; String slowState = ""; int aw, aa;   // ring = Elden Ring style (heavier timing)
        S(UUID id) { this.id = id; }
    }
    static final class MP { float poise, max; int delay, stagger; boolean riposte; MP(float max) { this.max = max; poise = max; } }

    private final Map<UUID, S> players = new ConcurrentHashMap<>();
    private final Map<UUID, MP> mobs = new HashMap<>();
    private final Set<UUID> dummies = new HashSet<>();
    private long tick;

    // ================================================================================================= on / off
    public boolean isOn(UUID id) { return players.containsKey(id); }

    public String setEnabled(ServerPlayer p, boolean on) { return setEnabled(p, on, false); }

    public String setEnabled(ServerPlayer p, boolean on, boolean ring) {
        S cur = players.get(p.getUUID());
        if (on && cur != null && cur.ring != ring) { cur.ring = ring; publish(p.getServer()); return NAME + " style " + (ring ? "elden ring" : "minecraft"); }
        if (on == isOn(p.getUUID())) return NAME + (on ? " already on" : " already off");
        if (on) {
            S s = new S(p.getUUID());
            s.ring = ring;
            players.put(p.getUUID(), s);
            sound(p, SoundEvents.ARMOR_EQUIP_NETHERITE, 0.9f, 0.8f);
            event(s, "combat on");
        } else {
            S s = players.remove(p.getUUID());
            slow(p, s, "");                                     // restore vanilla movement speed
            if (players.isEmpty()) cleanupWorld(p.getServer()); // staggers on mobs, debug dummies
        }
        publish(p.getServer());
        return NAME + (on ? " on" : " off");
    }

    /** Remove everything this mod put in the world: stagger effects on mobs and debug dummies. */
    public void cleanupWorld(MinecraftServer server) {
        if (server == null) return;
        for (ServerLevel l : server.getAllLevels()) {
            for (UUID u : new ArrayList<>(mobs.keySet())) {
                Entity e = l.getEntity(u);
                if (e instanceof LivingEntity le && mobs.get(u).stagger > 0) unstagger(le);
            }
            for (UUID u : dummies) { Entity e = l.getEntity(u); if (e != null) e.discard(); }
        }
        mobs.clear(); dummies.clear();
    }

    /** Client thread -> server thread: button counters, held buttons, stick and camera yaw. */
    public void input(UUID id, JsonObject j) {
        S s = players.get(id); if (s == null) return;
        if (j == null) { s.heavyHeld = false; s.blockHeld = false; s.fwd = s.strafe = 0; return; }
        s.inLight = l(j, "light"); s.inHeavy = l(j, "heavy"); s.inParry = l(j, "parry"); s.inDodge = l(j, "dodge");
        s.inLock = l(j, "lock"); s.inSwitch = l(j, "switch");
        s.heavyHeld = j.has("heavyHeld") && j.get("heavyHeld").getAsBoolean();
        s.blockHeld = j.has("block") && j.get("block").getAsBoolean();
        s.fwd = j.has("fwd") ? j.get("fwd").getAsFloat() : 0; s.strafe = j.has("strafe") ? j.get("strafe").getAsFloat() : 0;
        s.camYaw = j.has("yaw") ? j.get("yaw").getAsFloat() : 0;
    }
    private static long l(JsonObject j, String k) { return j.has(k) ? j.get(k).getAsLong() : -1; }

    // ================================================================================================= tick
    public void tick(MinecraftServer server) {
        tick++;
        if (players.isEmpty()) { if (VIEW.on) publish(server); if (!mobs.isEmpty() || !dummies.isEmpty()) cleanupWorld(server); return; }
        for (S s : players.values()) {
            ServerPlayer p = server.getPlayerList().getPlayer(s.id);
            if (p != null) tickPlayer(p, s);
        }
        tickMobs(server);
        publish(server);
    }

    private boolean edge(long in, long seen) { return in >= 0 && seen >= 0 && in > seen; }

    private void tickPlayer(ServerPlayer p, S s) {
        if (p.isDeadOrDying()) { if (s.act != Act.DEAD) { s.act = Act.DEAD; s.t = 0; s.lockId = -1; s.deathSeq++; event(s, "YOU DIED"); } slow(p, s, ""); return; }
        if (s.act == Act.DEAD) { s.act = Act.IDLE; s.stamina = s.maxStamina; s.poise = s.maxPoise; }
        // ---- input edges -> short buffers (Elden-style input buffering)
        if (edge(s.inLight, s.seenLight)) s.bufLight = 8;
        if (edge(s.inHeavy, s.seenHeavy)) s.bufHeavy = 8;
        if (edge(s.inDodge, s.seenDodge)) s.bufDodge = 6;
        if (edge(s.inParry, s.seenParry)) s.bufParry = 6;
        if (edge(s.inLock, s.seenLock)) toggleLock(p, s);
        if (edge(s.inSwitch, s.seenSwitch) && s.lockId >= 0) switchLock(p, s);
        s.seenLight = s.inLight; s.seenHeavy = s.inHeavy; s.seenDodge = s.inDodge; s.seenParry = s.inParry; s.seenLock = s.inLock; s.seenSwitch = s.inSwitch;

        s.weapon = ECWeapon.of(p.getMainHandItem());
        s.maxPoise = 30 + p.getArmorValue() * 2.5f;
        // ---- run the current action
        s.t++;
        switch (s.act) {
            case LIGHT, HEAVY, CHARGE -> tickAttack(p, s);
            case DODGE, BACKSTEP -> tickDodge(p, s);
            case RIPOSTE -> tickRiposte(p, s);
            case BLOCK -> { if (!s.blockHeld) idle(s); }
            case PARRY, GUARD_BREAK, STAGGER -> { if (s.t >= s.len) idle(s); }
            default -> { }
        }
        // ---- start the next action (dodge > parry > attacks > guard)
        if (canAct(s)) {
            if (s.bufDodge > 0 && s.stamina > 0) startDodge(p, s);
            else if (s.bufParry > 0 && s.stamina > 0) start(s, Act.PARRY, 16, 0);
            else if ((s.bufLight > 0 || s.bufHeavy > 0) && s.stamina > 0) {
                boolean heavy = s.bufHeavy > 0;
                LivingEntity crit = heavy ? null : riposteTarget(p, s);   // riposte = light attack on a staggered enemy
                if (crit != null) { start(s, Act.RIPOSTE, 26, 0); s.riposteTarget = crit.getId(); s.bufLight = s.bufHeavy = 0; }
                else startAttack(p, s, heavy);
            }
            else if (s.blockHeld && s.act == Act.IDLE) start(s, Act.BLOCK, 0, 0);
        }
        s.bufLight = Math.max(0, s.bufLight - 1); s.bufHeavy = Math.max(0, s.bufHeavy - 1);
        s.bufDodge = Math.max(0, s.bufDodge - 1); s.bufParry = Math.max(0, s.bufParry - 1);
        // ---- stamina / poise recovery
        if (p.isSprinting() && s.act == Act.IDLE && !p.isCreative()) { s.stamina -= 0.35f; s.staminaDelay = Math.max(s.staminaDelay, 10); if (s.stamina <= 0) p.setSprinting(false); }
        if (s.staminaDelay > 0) s.staminaDelay--;
        else if (s.act == Act.IDLE || s.act == Act.BLOCK) s.stamina = Math.min(s.maxStamina, s.stamina + (s.act == Act.BLOCK ? 0.7f : 2.2f));
        if (s.infinite) s.stamina = s.maxStamina;
        s.stamina = Math.max(-20, s.stamina);
        if (s.poiseDelay > 0) s.poiseDelay--; else s.poise = Math.min(s.maxPoise, s.poise + 2f);
        // ---- lock validity
        if (s.lockId >= 0) {
            Entity e = p.serverLevel().getEntity(s.lockId);
            if (!(e instanceof LivingEntity le) || !le.isAlive() || le.distanceTo(p) > 26) s.lockId = -1;
            else { if (p.hasLineOfSight(le)) s.lockLos = 0; else if (++s.lockLos > 60) s.lockId = -1; }
        }
        // ---- movement: slowed during commitments (attribute modifier, removed when idle / off)
        slow(p, s, switch (s.act) { case LIGHT, HEAVY, CHARGE, RIPOSTE, PARRY, GUARD_BREAK, STAGGER -> "attack"; case BLOCK -> "guard"; default -> ""; });
    }

    private boolean canAct(S s) {
        return switch (s.act) {
            case IDLE, BLOCK -> true;
            case LIGHT, HEAVY -> s.t >= windup(s) + active(s) + 2;                       // combo / cancel window in recovery
            case DODGE, BACKSTEP -> s.t >= s.len - 3;
            default -> false;
        };
    }
    private int windup(S s) { return s.aw + (s.heavy ? s.chargeTicks : 0); }
    private int active(S s) { return s.aa; }
    /** Elden Ring style: longer anticipation and recovery (weightier swings), same active frames. */
    private static int ringScale(S s, int ticks) { return s.ring ? Math.round(ticks * 1.35f) : ticks; }

    private void idle(S s) { s.act = Act.IDLE; s.t = 0; s.len = 0; }
    private void start(S s, Act a, int len, int unused) { s.act = a; s.t = 0; s.len = len; s.hit.clear(); if (a == Act.PARRY) { s.stamina -= 10; s.staminaDelay = 20; s.bufParry = 0; } }

    // ---- attacks
    private void startAttack(ServerPlayer p, S s, boolean heavy) {
        boolean chain = (s.act == Act.LIGHT || s.act == Act.HEAVY);
        s.combo = chain ? (s.combo + 1) % Math.max(1, s.weapon.combo) : 0;
        s.heavy = heavy; s.chargeTicks = 0; s.charge = 0;
        s.act = heavy ? Act.HEAVY : Act.LIGHT; s.t = 0; s.hit.clear();
        s.aw = ringScale(s, heavy ? s.weapon.hw : s.weapon.lw); s.aa = heavy ? s.weapon.ha : s.weapon.la;
        s.len = s.aw + s.aa + ringScale(s, heavy ? s.weapon.hr : s.weapon.lr);
        s.mv = heavy ? s.weapon.hmv : s.weapon.lmv * (s.combo == 2 ? 1.15 : 1.0);
        float cost = s.weapon.stamina * (heavy ? 1.5f : 1f);
        s.stamina -= cost; s.staminaDelay = 22;
        s.bufLight = s.bufHeavy = 0;
        s.attacks++; s.swingSeq++;
        sound(p, heavy ? SoundEvents.PLAYER_ATTACK_STRONG : SoundEvents.PLAYER_ATTACK_SWEEP, 0.6f, heavy ? 0.7f : 1.1f + s.combo * 0.08f);
    }

    private void tickAttack(ServerPlayer p, S s) {
        // heavy: holding the button at the end of the windup charges (up to 1 s) - Elden Ring charged R2
        if (s.heavy && s.heavyHeld && s.t == s.aw + s.chargeTicks - 1 && s.chargeTicks < 20) {
            s.chargeTicks++; s.len++; s.act = Act.CHARGE; s.charge = s.chargeTicks / 20f;
            if (s.chargeTicks % 5 == 0) p.serverLevel().sendParticles(ParticleTypes.ENCHANTED_HIT, p.getX(), p.getY() + 1.2, p.getZ(), 3, 0.3, 0.3, 0.3, 0.05);
            return;
        }
        if (s.act == Act.CHARGE) s.act = Act.HEAVY;
        int w = windup(s), a = active(s);
        if (s.t >= w && s.t < w + a) swingHits(p, s);
        if (s.t >= s.len) idle(s);
    }

    private Vec3 facing(ServerPlayer p, S s) {
        if (s.lockId >= 0) {
            Entity e = p.serverLevel().getEntity(s.lockId);
            if (e != null) { Vec3 d = e.position().subtract(p.position()); d = new Vec3(d.x, 0, d.z); if (d.lengthSqr() > 1e-4) return d.normalize(); }
        }
        double y = Math.toRadians(p.getYRot());
        return new Vec3(-Math.sin(y), 0, Math.cos(y));
    }

    private boolean targetable(Entity e, ServerPlayer p) {
        return e instanceof LivingEntity le && le != p && le.isAlive() && !(e instanceof ArmorStand) && !le.isSpectator()
            && !(e instanceof Player pl && (pl.isCreative() || !p.canHarmPlayer(pl)));
    }

    private void swingHits(ServerPlayer p, S s) {
        ServerLevel l = p.serverLevel();
        ECWeapon w = s.weapon;
        Vec3 fwd = facing(p, s), eye = p.position().add(0, 1.1, 0);
        if (s.hitboxes) {
            for (int i = -3; i <= 3; i++) {
                double a = Math.toRadians(w.arc * 0.5 * i / 3.0);
                Vec3 d = new Vec3(fwd.x * Math.cos(a) - fwd.z * Math.sin(a), 0, fwd.x * Math.sin(a) + fwd.z * Math.cos(a));
                Vec3 q = eye.add(d.scale(w.reach));
                l.sendParticles(new DustParticleOptions(new Vector3f(1f, 0.85f, 0.2f), 0.8f), q.x, q.y, q.z, 1, 0, 0, 0, 0);
            }
        }
        AABB box = p.getBoundingBox().inflate(w.reach + 1, 1.5, w.reach + 1);
        for (Entity e : l.getEntities(p, box, e -> targetable(e, p))) {
            LivingEntity le = (LivingEntity) e;
            if (s.hit.contains(le.getId())) continue;
            Vec3 c = le.position().add(0, le.getBbHeight() * 0.5, 0), to = c.subtract(eye), h = new Vec3(to.x, 0, to.z);
            double dist = h.length() - le.getBbWidth() * 0.5;
            if (dist > w.reach || Math.abs(to.y) > 1.6 + le.getBbHeight() * 0.5) continue;
            if (h.lengthSqr() > 0.25) {
                double ang = Math.toDegrees(Math.acos(Math.max(-1, Math.min(1, h.normalize().dot(fwd)))));
                if (ang > w.arc * 0.5 + (dist < 1.0 ? 30 : 0)) continue;
            }
            if (!p.hasLineOfSight(le)) continue;
            s.hit.add(le.getId());
            float dmg = (float) (baseDamage(p, le) * s.mv * (1 + s.charge * 0.6));
            dealDamage(p, s, le, dmg, w.poise * (s.heavy ? 1.6f + s.charge : 1f), s.heavy ? 0.7f : 0.25f, false);
        }
    }

    private float baseDamage(ServerPlayer p, LivingEntity target) {
        ItemStack st = p.getMainHandItem();
        float d = (float) p.getAttributeValue(Attributes.ATTACK_DAMAGE);
        d += EnchantmentHelper.getDamageBonus(st, target.getMobType());
        return Math.max(1f, d);
    }

    private void dealDamage(ServerPlayer p, S s, LivingEntity le, float dmg, float poiseDmg, float knock, boolean crit) {
        ServerLevel l = p.serverLevel();
        le.invulnerableTime = 0;
        float before = le.getHealth();
        if (!le.hurt(p.damageSources().playerAttack(p), dmg)) return;
        float dealt = Math.max(0, before - le.getHealth());
        Vec3 fwd = le.position().subtract(p.position()); fwd = new Vec3(fwd.x, 0, fwd.z);
        if (knock > 0 && fwd.lengthSqr() > 1e-4) { fwd = fwd.normalize(); le.knockback(knock, -fwd.x, -fwd.z); }
        p.setLastHurtMob(le);
        ItemStack st = p.getMainHandItem();
        if (!st.isEmpty() && st.isDamageableItem()) st.hurtAndBreak(1, p, pl -> pl.broadcastBreakEvent(net.minecraft.world.InteractionHand.MAIN_HAND));
        s.hits++; s.hitSeq++; s.lastDamage = dealt > 0 ? dealt : dmg; s.damageSeq++;
        s.damageAt = le.position().add(0, le.getBbHeight() + 0.3, 0);
        Vec3 c = le.position().add(0, le.getBbHeight() * 0.6, 0);
        l.sendParticles(crit ? ParticleTypes.ENCHANTED_HIT : ParticleTypes.CRIT, c.x, c.y, c.z, crit ? 24 : 8, 0.2, 0.25, 0.2, 0.3);
        l.sendParticles(new DustParticleOptions(new Vector3f(0.6f, 0.02f, 0.02f), 1.1f), c.x, c.y, c.z, 6, 0.15, 0.2, 0.15, 0);
        sound(p, crit ? SoundEvents.PLAYER_ATTACK_CRIT : SoundEvents.PLAYER_ATTACK_KNOCKBACK, 0.8f, crit ? 0.6f : 1.0f);
        if (!le.isAlive()) {
            s.killSeq++;
            s.runes += Math.round(le.getMaxHealth() * 10 + (crit ? 50 : 0));
            if (le.getMaxHealth() >= 40 || le.getId() == s.lockId) event(s, "ENEMY FELLED");
            if (le.getId() == s.lockId) s.lockId = -1;
            return;
        }
        mobPoise(l, s, le, poiseDmg);
    }

    // ---- riposte (critical on a staggered enemy)
    private LivingEntity riposteTarget(ServerPlayer p, S s) {
        Vec3 fwd = facing(p, s);
        LivingEntity best = null; double bestD = 99;
        for (Entity e : p.serverLevel().getEntities(p, p.getBoundingBox().inflate(3.5, 1.5, 3.5), e -> targetable(e, p))) {
            MP m = mobs.get(e.getUUID());
            if (m == null || m.stagger <= 0 || !m.riposte) continue;
            Vec3 h = e.position().subtract(p.position()); h = new Vec3(h.x, 0, h.z);
            double d = h.length();
            if (d > 3.4 || (d > 0.4 && h.normalize().dot(fwd) < 0.35)) continue;
            if (d < bestD) { bestD = d; best = (LivingEntity) e; }
        }
        return best;
    }

    private void tickRiposte(ServerPlayer p, S s) {
        Entity e = p.serverLevel().getEntity(s.riposteTarget);
        if (s.t == 1) { s.stamina -= 8; s.staminaDelay = 20; s.attacks++; s.swingSeq++; sound(p, SoundEvents.PLAYER_ATTACK_SWEEP, 0.8f, 0.6f); }
        if (s.t == 11 && e instanceof LivingEntity le && le.isAlive()) {
            MP m = mobs.get(le.getUUID());
            if (m != null) { m.stagger = 0; m.riposte = false; unstagger(le); }
            float dmg = baseDamage(p, le) * (float) (s.weapon == ECWeapon.FIST ? 2.6 : 3.2);
            s.riposteSeq++; event(s, "CRITICAL");
            dealDamage(p, s, le, dmg, 0, 0.9f, true);
        }
        if (s.t >= s.len) idle(s);
    }

    // ---- dodge roll / backstep
    private void startDodge(ServerPlayer p, S s) {
        double y = Math.toRadians(s.camYaw);
        Vec3 f = new Vec3(-Math.sin(y), 0, Math.cos(y)), r = new Vec3(-Math.cos(y), 0, -Math.sin(y));
        Vec3 wish = f.scale(s.fwd).add(r.scale(s.strafe));
        boolean back = wish.lengthSqr() < 0.04;
        if (back) { double py = Math.toRadians(p.getYRot()); wish = new Vec3(Math.sin(py), 0, -Math.cos(py)); }
        s.dodgeDir = wish.normalize();
        s.act = back ? Act.BACKSTEP : Act.DODGE; s.t = 0; s.len = back ? (s.ring ? 11 : 9) : (s.ring ? 18 : 15);
        s.stamina -= back ? 8 : 15; s.staminaDelay = 20; s.bufDodge = 0; s.dodges++;
        sound(p, SoundEvents.ARMOR_EQUIP_LEATHER, 0.8f, back ? 1.3f : 0.9f);
    }
    private boolean iframes(S s) {
        return (s.act == Act.DODGE && s.t >= (s.ring ? 2 : 1) && s.t <= (s.ring ? 11 : 10)) || (s.act == Act.BACKSTEP && s.t >= 1 && s.t <= 6) || s.act == Act.RIPOSTE;
    }
    private void tickDodge(ServerPlayer p, S s) {
        boolean back = s.act == Act.BACKSTEP;
        if (s.t <= (back ? 4 : 8)) {
            double sp = back ? 0.42 : 0.46;
            Vec3 v = p.getDeltaMovement();
            p.setDeltaMovement(s.dodgeDir.x * sp, Math.min(v.y, p.onGround() ? 0 : v.y), s.dodgeDir.z * sp);
            p.hurtMarked = true;
        }
        if (s.t >= s.len) idle(s);
    }
    private boolean parryWindow(ServerPlayer p, S s) { return s.act == Act.PARRY && s.t >= 2 && s.t <= (hasShield(p) ? 9 : 6); }
    private static boolean hasShield(ServerPlayer p) { return p.getOffhandItem().getItem() instanceof ShieldItem || p.getMainHandItem().getItem() instanceof ShieldItem; }

    // ================================================================================================= mobs: poise + stagger
    private float maxPoise(LivingEntity le) { return 8 + le.getMaxHealth() * 0.9f; }

    private void mobPoise(ServerLevel l, S s, LivingEntity le, float dmg) {
        MP m = mobs.computeIfAbsent(le.getUUID(), k -> new MP(maxPoise(le)));
        if (m.stagger > 0) return;
        m.poise -= dmg; m.delay = 100;
        if (m.poise <= 0) stagger(l, s, le, m, 50, true);
    }

    private void stagger(ServerLevel l, S s, LivingEntity le, MP m, int ticks, boolean riposte) {
        m.stagger = ticks; m.poise = m.max; m.riposte = riposte;
        le.addEffect(new MobEffectInstance(MobEffects.MOVEMENT_SLOWDOWN, ticks, 6, false, false, false));
        le.addEffect(new MobEffectInstance(MobEffects.WEAKNESS, ticks, 9, false, false, false));
        if (le instanceof Mob mob) mob.getNavigation().stop();
        if (s != null) { s.mobStaggerSeq++; event(s, "STAGGER"); }
        Vec3 c = le.position().add(0, le.getBbHeight() + 0.2, 0);
        l.sendParticles(ParticleTypes.ENCHANTED_HIT, c.x, c.y, c.z, 14, 0.3, 0.1, 0.3, 0.2);
        l.playSound(null, le.getX(), le.getY(), le.getZ(), SoundEvents.ANVIL_LAND, SoundSource.HOSTILE, 0.45f, 1.6f);
    }

    private void unstagger(LivingEntity le) {
        MobEffectInstance a = le.getEffect(MobEffects.MOVEMENT_SLOWDOWN), b = le.getEffect(MobEffects.WEAKNESS);
        if (a != null && a.getAmplifier() == 6 && !a.isVisible()) le.removeEffect(MobEffects.MOVEMENT_SLOWDOWN);
        if (b != null && b.getAmplifier() == 9 && !b.isVisible()) le.removeEffect(MobEffects.WEAKNESS);
    }

    private void tickMobs(MinecraftServer server) {
        for (Iterator<Map.Entry<UUID, MP>> it = mobs.entrySet().iterator(); it.hasNext(); ) {
            MP m = it.next().getValue();
            if (m.stagger > 0) { if (--m.stagger == 0) m.riposte = false; continue; }
            if (m.delay > 0) m.delay--; else m.poise = Math.min(m.max, m.poise + 0.5f);
        }
        if (tick % 200 == 0) {
            Set<UUID> alive = new HashSet<>();
            for (ServerLevel l : server.getAllLevels()) for (UUID u : mobs.keySet()) if (l.getEntity(u) != null) alive.add(u);
            mobs.keySet().retainAll(alive);
        }
    }

    // ================================================================================================= incoming damage
    /** Player.hurt hook (server thread): i-frames, parry, guard, player poise. Returns the damage that goes through. */
    public float incoming(ServerPlayer p, DamageSource src, float amt) {
        S s = players.get(p.getUUID());
        if (s == null || amt <= 0 || p.isCreative()) return amt;
        Entity direct = src.getDirectEntity(), att = src.getEntity();
        if (direct == null || src.is(DamageTypeTags.BYPASSES_INVULNERABILITY) || src.is(DamageTypeTags.IS_FIRE) || src.is(DamageTypeTags.IS_EXPLOSION) && direct == null) return amt;
        if (iframes(s)) { s.dodgeSeq++; event(s, "DODGED"); return 0; }
        Vec3 to = direct.position().subtract(p.position()); to = new Vec3(to.x, 0, to.z);
        double y = Math.toRadians(p.getYRot());
        boolean front = to.lengthSqr() < 1e-4 || to.normalize().dot(new Vec3(-Math.sin(y), 0, Math.cos(y))) > 0.2;
        if (parryWindow(p, s) && front) {
            s.parrySeq++; event(s, "PARRY");
            sound(p, SoundEvents.SHIELD_BLOCK, 1f, 1.5f); sound(p, SoundEvents.ANVIL_PLACE, 0.4f, 2f);
            if (att instanceof LivingEntity le && !(direct instanceof Projectile) && le.distanceTo(p) < 5) {
                MP m = mobs.computeIfAbsent(le.getUUID(), k -> new MP(maxPoise(le)));
                stagger(p.serverLevel(), s, le, m, 60, true);
            }
            return 0;
        }
        if (s.act == Act.BLOCK && front) {
            boolean shield = hasShield(p);
            float through = amt * (shield ? 0f : 0.4f);
            s.stamina -= amt * (shield ? 3.5f : 5f) + 6; s.staminaDelay = 25; s.blockSeq++;
            sound(p, SoundEvents.SHIELD_BLOCK, 0.9f, shield ? 1f : 1.4f);
            if (att instanceof LivingEntity le && !(direct instanceof Projectile)) le.knockback(0.35, p.getX() - le.getX(), p.getZ() - le.getZ());
            if (s.stamina <= 0) {
                s.stamina = 0; s.act = Act.GUARD_BREAK; s.t = 0; s.len = 30; s.guardBreakSeq++; event(s, "GUARD BROKEN");
                sound(p, SoundEvents.SHIELD_BREAK, 1f, 0.9f);
                return Math.max(through, amt * 0.5f);
            }
            return through;
        }
        // hit: player poise (heavy attacks carry hyper armor through the windup and swing)
        s.hurtSeq++;
        float pd = amt * 4f;
        if ((s.act == Act.HEAVY || s.act == Act.CHARGE) && s.t >= 2) pd *= 0.35f;
        s.poise -= pd; s.poiseDelay = 60;
        if (s.poise <= 0) { s.poise = s.maxPoise; s.act = Act.STAGGER; s.t = 0; s.len = 14; s.staggerSeq++; }
        return amt;
    }

    // ================================================================================================= lock-on
    private void toggleLock(ServerPlayer p, S s) {
        if (s.lockId >= 0) { s.lockId = -1; return; }
        LivingEntity t = pickTarget(p, s, null);
        s.lockId = t == null ? -1 : t.getId(); s.lockLos = 0;
        if (t != null) sound(p, SoundEvents.UI_BUTTON_CLICK.value(), 0.25f, 1.8f);
    }
    private void switchLock(ServerPlayer p, S s) {
        LivingEntity t = pickTarget(p, s, p.serverLevel().getEntity(s.lockId));
        if (t != null) { s.lockId = t.getId(); s.lockLos = 0; }
    }
    private LivingEntity pickTarget(ServerPlayer p, S s, Entity except) {
        double y = Math.toRadians(s.camYaw);
        Vec3 f = new Vec3(-Math.sin(y), 0, Math.cos(y));
        LivingEntity best = null; double bestScore = 1e9;
        for (Entity e : p.serverLevel().getEntities(p, p.getBoundingBox().inflate(20, 8, 20), e -> targetable(e, p))) {
            if (e == except) continue;
            Vec3 d = e.position().subtract(p.position()); Vec3 h = new Vec3(d.x, 0, d.z);
            double dist = d.length(); if (dist > 20 || h.lengthSqr() < 1e-4) continue;
            double dot = h.normalize().dot(f); if (dot < 0.25) continue;
            if (!p.hasLineOfSight(e)) continue;
            double score = dist * 0.6 + (1 - dot) * 18 + (e instanceof net.minecraft.world.entity.monster.Enemy ? 0 : 6);
            if (score < bestScore) { bestScore = score; best = (LivingEntity) e; }
        }
        return best;
    }

    // ================================================================================================= helpers
    private void slow(ServerPlayer p, S s, String mode) {
        if (s != null && mode.equals(s.slowState)) return;
        if (s != null) s.slowState = mode;
        AttributeInstance sp = p.getAttribute(Attributes.MOVEMENT_SPEED);
        if (sp == null) return;
        sp.removeModifier(SLOW_ID);
        if (!mode.isEmpty()) sp.addTransientModifier(new AttributeModifier(SLOW_ID, "elden combat commitment", mode.equals("guard") ? -0.45 : -0.7, AttributeModifier.Operation.MULTIPLY_TOTAL));
    }
    private void event(S s, String e) { s.event = e; s.eventSeq++; }
    private void sound(ServerPlayer p, SoundEvent e, float vol, float pitch) { p.serverLevel().playSound(null, p.getX(), p.getY(), p.getZ(), e, SoundSource.PLAYERS, vol, pitch); }

    public void onPlayerDeath(ServerPlayer p) { S s = players.get(p.getUUID()); if (s != null) { s.act = Act.DEAD; s.t = 0; s.lockId = -1; s.deathSeq++; event(s, "YOU DIED"); } }
    public void onRespawn(ServerPlayer p) { S s = players.get(p.getUUID()); if (s != null) { s.act = Act.IDLE; s.stamina = s.maxStamina; s.poise = s.maxPoise; s.slowState = "x"; slow(p, s, ""); } }

    // ================================================================================================= ops (menu, debug, tests)
    public JsonObject op(ServerPlayer p, String op, JsonObject args, boolean dev) {
        JsonObject r = new JsonObject();
        S s = players.get(p.getUUID());
        String msg;
        switch (op) {
            case "ec.on" -> msg = setEnabled(p, true, args.has("style") && args.get("style").getAsString().equals("ring"));
            case "ec.off" -> msg = setEnabled(p, false);
            case "ec.status" -> {
                AttributeInstance sp = p.getAttribute(Attributes.MOVEMENT_SPEED);
                boolean slowed = sp != null && sp.getModifier(SLOW_ID) != null;
                r.addProperty("on", s != null); r.addProperty("dummies", dummies.size()); r.addProperty("mobs", mobs.size()); r.addProperty("slowed", slowed);
                int stag = 0;
                for (Entity e : p.serverLevel().getEntities(p, p.getBoundingBox().inflate(32), e -> e instanceof LivingEntity)) {
                    MobEffectInstance m = ((LivingEntity) e).getEffect(MobEffects.MOVEMENT_SLOWDOWN);
                    if (m != null && m.getAmplifier() == 6 && !m.isVisible()) stag++;
                    if (e.getTags().contains(DUMMY_TAG)) r.addProperty("dummyEntities", (r.has("dummyEntities") ? r.get("dummyEntities").getAsInt() : 0) + 1);
                }
                r.addProperty("staggeredMobs", stag);
                msg = (s == null ? "off" : "on " + s.act + " " + s.weapon.key) + " dummies " + dummies.size() + " slowed " + slowed;
            }
            default -> {
                if (s == null) { r.addProperty("ok", false); r.addProperty("message", NAME + " is off: debug tools are unavailable"); return r; }
                if (!dev && !op.equals("ec.debug")) { r.addProperty("ok", false); r.addProperty("message", op + " is limited to this project's dev worlds"); return r; }
                msg = debugOp(p, s, op, args);
                if (msg == null) { r.addProperty("ok", false); r.addProperty("message", "unknown op " + op); return r; }
            }
        }
        publish(p.getServer());
        r.addProperty("ok", true); r.addProperty("message", msg);
        return r;
    }

    private String debugOp(ServerPlayer p, S s, String op, JsonObject a) {
        ServerLevel l = p.serverLevel();
        switch (op) {
            case "ec.debug" -> {   // flags only (also allowed outside the dev worlds)
                if (a.has("infinite")) s.infinite = a.get("infinite").getAsBoolean();
                if (a.has("hitboxes")) s.hitboxes = a.get("hitboxes").getAsBoolean();
                return "debug flags: infinite stamina " + s.infinite + ", hitboxes " + s.hitboxes;
            }
            case "ec.kit" -> {
                for (ItemStack st : new ItemStack[] { new ItemStack(Items.IRON_SWORD), new ItemStack(Items.IRON_AXE), new ItemStack(Items.IRON_PICKAXE),
                        new ItemStack(Items.TRIDENT), new ItemStack(Items.IRON_HOE), new ItemStack(Items.DIAMOND_SWORD) })
                    if (!p.getInventory().add(st)) p.drop(st, false);
                if (p.getOffhandItem().isEmpty()) p.setItemSlot(net.minecraft.world.entity.EquipmentSlot.OFFHAND, new ItemStack(Items.SHIELD));
                return "weapon kit given";
            }
            case "ec.select" -> {
                String want = a.has("item") ? a.get("item").getAsString() : "minecraft:iron_sword";
                var item = BuiltInRegistries.ITEM.get(new net.minecraft.resources.ResourceLocation(want));
                p.getInventory().selected = 0;
                p.getInventory().setItem(0, item == Items.AIR ? ItemStack.EMPTY : new ItemStack(item));
                return "holding " + want;
            }
            case "ec.shield" -> { boolean on = !a.has("on") || a.get("on").getAsBoolean(); p.setItemSlot(net.minecraft.world.entity.EquipmentSlot.OFFHAND, on ? new ItemStack(Items.SHIELD) : ItemStack.EMPTY); return "shield " + on; }
            case "ec.dummy" -> {
                String kind = a.has("kind") ? a.get("kind").getAsString() : "zombie";
                double dist = a.has("dist") ? a.get("dist").getAsDouble() : 3.0;
                EntityType<?> type = switch (kind) { case "husk" -> EntityType.HUSK; case "skeleton" -> EntityType.SKELETON; case "golem" -> EntityType.IRON_GOLEM; case "vindicator" -> EntityType.VINDICATOR; default -> EntityType.ZOMBIE; };
                double y = Math.toRadians(a.has("yaw") ? a.get("yaw").getAsDouble() : p.getYRot());
                Vec3 at = p.position().add(-Math.sin(y) * dist, 0, Math.cos(y) * dist);
                Entity e = type.create(l);
                if (e == null) return "spawn failed";
                float face = (float) Math.toDegrees(Math.atan2(-Math.sin(y), -Math.cos(y)));   // looking back at the player
                e.moveTo(at.x, at.y, at.z, face, 0); e.setYHeadRot(face); e.setYBodyRot(face);
                if (e instanceof Mob m) {
                    m.finalizeSpawn(l, l.getCurrentDifficultyAt(m.blockPosition()), MobSpawnType.COMMAND, null, null);
                    m.setPersistenceRequired();
                    if (a.has("passive") && a.get("passive").getAsBoolean()) m.setNoAi(true);
                    m.setItemSlot(net.minecraft.world.entity.EquipmentSlot.HEAD, new ItemStack(Items.LEATHER_HELMET));   // no sun burning
                }
                e.addTag(DUMMY_TAG);
                l.addFreshEntity(e); dummies.add(e.getUUID());
                return "spawned " + kind + " #" + e.getId();
            }
            case "ec.clearDummies" -> { for (UUID u : dummies) { Entity e = l.getEntity(u); if (e != null) e.discard(); } int n = dummies.size(); dummies.clear(); return "removed " + n + " dummies"; }
            case "ec.stamina" -> { s.stamina = a.has("value") ? a.get("value").getAsFloat() : s.maxStamina; return "stamina " + s.stamina; }
            case "ec.breakPoise" -> {
                Entity e = s.lockId >= 0 ? l.getEntity(s.lockId) : pickTarget(p, s, null);
                if (!(e instanceof LivingEntity le)) return "no target";
                stagger(l, s, le, mobs.computeIfAbsent(le.getUUID(), k -> new MP(maxPoise(le))), 80, true);
                return "staggered #" + le.getId();
            }
            case "ec.mobHit" -> {   // the nearest mob lands a real melee hit on the player (tests i-frames / guard / parry)
                Mob best = null; double bd = 9;
                for (Entity e : l.getEntities(p, p.getBoundingBox().inflate(8), e -> e instanceof Mob m && m.isAlive())) { double d = e.distanceTo(p); if (d < bd) { bd = d; best = (Mob) e; } }
                if (best == null) return "no mob near";
                p.invulnerableTime = 0;
                float before = p.getHealth();
                boolean ok = best.doHurtTarget(p);
                return "mob hit " + ok + " health " + before + " -> " + p.getHealth();
            }
            case "ec.lock" -> { if (a.has("id")) { s.lockId = a.get("id").getAsInt(); } else toggleLock(p, s); return "lock " + s.lockId; }
            case "ec.heal" -> { p.setHealth(p.getMaxHealth()); return "healed"; }
            default -> { return null; }
        }
    }

    // ================================================================================================= view (exported by the client thread as "ec")
    public static final class View {
        boolean on; String act = "IDLE"; int t, len, combo, w, a; float charge; String weapon = "fist", item = ""; boolean shield, iframes, parry;
        float stamina, maxStamina, poise, maxPoise, hp, maxHp; long runes; int attacks, hits, dodges;
        int lockId = -1; double lx, ly, lz; float lh, lw, lhp, lmax, lpoise, lpmax; boolean lstagger; String lname = "";
        long hitSeq, parrySeq, blockSeq, guardBreakSeq, dodgeSeq, riposteSeq, staggerSeq, hurtSeq, swingSeq, killSeq, deathSeq, eventSeq, damageSeq, mobStaggerSeq;
        String event = ""; float lastDamage; double dx, dy, dz; boolean infinite, hitboxes, ring; long tick; int mobsTracked, dummies;
    }
    public static volatile View VIEW = new View();

    private void publish(MinecraftServer server) {
        S s = players.values().stream().findFirst().orElse(null);
        if (s == null) { if (VIEW.on) VIEW = new View(); return; }
        ServerPlayer p = server.getPlayerList().getPlayer(s.id);
        View v = new View();
        v.on = true; v.act = s.act.name(); v.t = s.t; v.len = s.len; v.combo = s.combo; v.w = windup(s); v.a = active(s); v.charge = s.charge; v.weapon = s.weapon.key;
        v.stamina = s.stamina; v.maxStamina = s.maxStamina; v.poise = s.poise; v.maxPoise = s.maxPoise; v.runes = s.runes;
        v.attacks = s.attacks; v.hits = s.hits; v.dodges = s.dodges; v.iframes = iframes(s);
        if (p != null) {
            v.hp = p.getHealth(); v.maxHp = p.getMaxHealth(); v.shield = hasShield(p); v.parry = parryWindow(p, s);
            ItemStack st = p.getMainHandItem(); v.item = st.isEmpty() ? "" : BuiltInRegistries.ITEM.getKey(st.getItem()).toString();
            if (s.lockId >= 0 && p.serverLevel().getEntity(s.lockId) instanceof LivingEntity le) {
                v.lockId = le.getId(); v.lx = le.getX(); v.ly = le.getY(); v.lz = le.getZ(); v.lh = le.getBbHeight(); v.lw = le.getBbWidth();
                v.lhp = le.getHealth(); v.lmax = le.getMaxHealth(); v.lname = le.getName().getString();
                MP m = mobs.get(le.getUUID()); v.lpmax = m == null ? maxPoise(le) : m.max; v.lpoise = m == null ? v.lpmax : m.poise; v.lstagger = m != null && m.stagger > 0;
            }
        }
        v.hitSeq = s.hitSeq; v.parrySeq = s.parrySeq; v.blockSeq = s.blockSeq; v.guardBreakSeq = s.guardBreakSeq; v.dodgeSeq = s.dodgeSeq;
        v.riposteSeq = s.riposteSeq; v.staggerSeq = s.staggerSeq; v.hurtSeq = s.hurtSeq; v.swingSeq = s.swingSeq; v.killSeq = s.killSeq;
        v.deathSeq = s.deathSeq; v.eventSeq = s.eventSeq; v.damageSeq = s.damageSeq; v.mobStaggerSeq = s.mobStaggerSeq; v.event = s.event; v.lastDamage = s.lastDamage;
        v.dx = s.damageAt.x; v.dy = s.damageAt.y; v.dz = s.damageAt.z; v.infinite = s.infinite; v.hitboxes = s.hitboxes; v.ring = s.ring; v.tick = tick;
        v.mobsTracked = mobs.size(); v.dummies = dummies.size();
        VIEW = v;
    }

    public static JsonObject viewJson() {
        View v = VIEW; JsonObject j = new JsonObject();
        j.addProperty("on", v.on); if (!v.on) return j;
        j.addProperty("act", v.act); j.addProperty("t", v.t); j.addProperty("w", v.w); j.addProperty("a", v.a); j.addProperty("len", v.len); j.addProperty("combo", v.combo); j.addProperty("charge", v.charge);
        j.addProperty("weapon", v.weapon); j.addProperty("item", v.item); j.addProperty("shield", v.shield); j.addProperty("iframes", v.iframes); j.addProperty("parry", v.parry);
        j.addProperty("stamina", v.stamina); j.addProperty("maxStamina", v.maxStamina); j.addProperty("poise", v.poise); j.addProperty("maxPoise", v.maxPoise);
        j.addProperty("hp", v.hp); j.addProperty("maxHp", v.maxHp); j.addProperty("runes", v.runes);
        j.addProperty("attacks", v.attacks); j.addProperty("hits", v.hits); j.addProperty("dodges", v.dodges);
        if (v.lockId >= 0) {
            JsonObject l = new JsonObject();
            l.addProperty("id", v.lockId); l.addProperty("x", v.lx); l.addProperty("y", v.ly); l.addProperty("z", v.lz); l.addProperty("h", v.lh); l.addProperty("w", v.lw);
            l.addProperty("hp", v.lhp); l.addProperty("max", v.lmax); l.addProperty("poise", v.lpoise); l.addProperty("poiseMax", v.lpmax); l.addProperty("stagger", v.lstagger); l.addProperty("name", v.lname);
            j.add("lock", l);
        }
        j.addProperty("hitSeq", v.hitSeq); j.addProperty("parrySeq", v.parrySeq); j.addProperty("blockSeq", v.blockSeq); j.addProperty("guardBreakSeq", v.guardBreakSeq);
        j.addProperty("dodgeSeq", v.dodgeSeq); j.addProperty("riposteSeq", v.riposteSeq); j.addProperty("staggerSeq", v.staggerSeq); j.addProperty("hurtSeq", v.hurtSeq);
        j.addProperty("swingSeq", v.swingSeq); j.addProperty("killSeq", v.killSeq); j.addProperty("deathSeq", v.deathSeq); j.addProperty("eventSeq", v.eventSeq);
        j.addProperty("damageSeq", v.damageSeq); j.addProperty("mobStaggerSeq", v.mobStaggerSeq); j.addProperty("event", v.event); j.addProperty("lastDamage", v.lastDamage);
        j.addProperty("dx", v.dx); j.addProperty("dy", v.dy); j.addProperty("dz", v.dz);
        j.addProperty("style", v.ring ? "ring" : "minecraft"); j.addProperty("infinite", v.infinite); j.addProperty("hitboxes", v.hitboxes); j.addProperty("tick", v.tick);
        j.addProperty("mobsTracked", v.mobsTracked); j.addProperty("dummies", v.dummies);
        return j;
    }

    public void clear() { players.clear(); mobs.clear(); dummies.clear(); VIEW = new View(); }
}
