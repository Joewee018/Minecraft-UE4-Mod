package crb.zm;

import crb.BridgeMod;
import net.minecraft.core.BlockPos;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.FloatTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.nbt.NbtUtils;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.entity.Display;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

/**
 * The random weapon box (original design in the style of a round-based-zombies mystery box), built from display
 * entities so every motion is a smooth client-side interpolation that the Unreal pose export picks up:
 *   idle    - closed box (crossover_rebuilt:mystery_box + _lid) with a light beam above it
 *   open    - lid swings back on its hinge, guns cycle while rising out of the box (slowing down), the result floats
 *   take    - F while it floats gives the gun; ignored for 12 s it sinks back and the lid closes
 *   leave   - sometimes a bad omen rises instead: refund, the box lifts away spinning and appears at another spot
 * Server thread only. Interaction and cost live in ZombiesGame (station kind CRATE).
 */
public final class MysteryBox {
    public enum Phase { IDLE, OPENING, CYCLING, OFFER, CLOSING, LEAVING, GONE }

    final List<BlockPos> spots = new ArrayList<>();
    int spot;
    BlockPos at;                     // box origin block (the box spans at..at+x1)
    Phase phase = Phase.IDLE;
    int ticks, uses, cycleIndex, nextSwap;
    boolean badOmen;
    Display.BlockDisplay base, lid, beam;
    Display.ItemDisplay item;
    public ItemStack offered = ItemStack.EMPTY;
    public String offeredName = "";

    static final float LID_OPEN_DEG = 105f;
    static final int OPEN_TICKS = 8, CYCLE_TICKS = 70, OFFER_TICKS = 240, CLOSE_TICKS = 10, LEAVE_TICKS = 45;

    public BlockPos pos() { return at; }
    public Phase phase() { return phase; }
    public boolean busy() { return phase != Phase.IDLE; }

    // ------------------------------------------------------------------ build / remove
    void place(ServerLevel l, List<BlockPos> candidates, int index) {
        spots.clear(); spots.addAll(candidates);
        spot = Math.floorMod(index, spots.size());
        at = spots.get(spot);
        build(l);
    }

    void build(ServerLevel l) {
        discard();
        base = blockDisplay(l, BridgeMod.MYSTERY_BOX.defaultBlockState(), Vec3.atLowerCornerOf(at), transform(0, 0, 0, 1, 1, 1, 0));
        lid = blockDisplay(l, BridgeMod.MYSTERY_BOX_LID.defaultBlockState(), Vec3.atLowerCornerOf(at), lidTransform(0f));
        CompoundTag bright = new CompoundTag(); bright.putInt("sky", 15); bright.putInt("block", 15);
        beam = blockDisplay(l, Blocks.LIGHT_BLUE_STAINED_GLASS.defaultBlockState(), Vec3.atLowerCornerOf(at), transform(0.85f, 0.75f, 0.42f, 0.3f, 14f, 0.16f, 0));
        CompoundTag b = new CompoundTag(); b.put("brightness", bright); merge(beam, b);
        phase = Phase.IDLE; ticks = 0;
    }

    void discard() {
        for (Entity e : new Entity[] { base, lid, beam, item }) if (e != null && !e.isRemoved()) e.discard();
        base = lid = beam = null; item = null;
    }

    // ------------------------------------------------------------------ use
    /** Starts a roll. Returns false when busy. badOmen = this roll makes the box leave. */
    boolean open(ServerLevel l, boolean omen) {
        if (phase != Phase.IDLE || lid == null) return false;
        uses++;
        badOmen = omen;
        phase = Phase.OPENING; ticks = 0;
        animate(lid, lidTransform(LID_OPEN_DEG), OPEN_TICKS);
        l.playSound(null, at, SoundEvents.CHEST_OPEN, SoundSource.BLOCKS, 1f, 0.6f);
        l.playSound(null, at, SoundEvents.AMETHYST_BLOCK_CHIME, SoundSource.BLOCKS, 2f, 0.5f);
        return true;
    }

    /** Ticks the animation. pick supplies the cycling display stacks (index -> stack); result is the final roll. */
    void tick(ServerLevel l, Function<Integer, ItemStack> pick, ItemStack result, String resultName) {
        ticks++;
        switch (phase) {
            case OPENING -> {
                if (ticks >= OPEN_TICKS) {
                    phase = Phase.CYCLING; ticks = 0; cycleIndex = 0; nextSwap = 0;
                    Vec3 c = Vec3.atLowerCornerOf(at).add(1.0, 0.55, 0.5);
                    item = itemDisplay(l, pick.apply(0), c);
                    animate(item, transform(0, 0.95f, 0, 0.85f, 0.85f, 0.85f, 0), CYCLE_TICKS); // rises out over the roll
                }
            }
            case CYCLING -> {
                // Swaps slow down towards the end, like a wheel coming to rest.
                if (ticks >= nextSwap && ticks < CYCLE_TICKS - 6) {
                    cycleIndex++;
                    setItem(item, pick.apply(cycleIndex));
                    nextSwap = ticks + 2 + ticks / 12;
                    if (cycleIndex % 2 == 0) l.playSound(null, at, SoundEvents.NOTE_BLOCK_CHIME.value(), SoundSource.BLOCKS, 0.7f, 0.7f + ticks / (float) CYCLE_TICKS);
                }
                if (ticks >= CYCLE_TICKS) {
                    ticks = 0;
                    if (badOmen) {
                        setItem(item, new ItemStack(Items.WITHER_SKELETON_SKULL));
                        offered = ItemStack.EMPTY; offeredName = "Bye bye!";
                        l.playSound(null, at, SoundEvents.WITCH_CELEBRATE, SoundSource.HOSTILE, 1.5f, 0.6f);
                        phase = Phase.LEAVING;
                        // lift away: box and lid rise and spin, beam fades out (shrinks)
                        animate(base, transform(0, 4f, 0, 1, 1, 1, 540f), LEAVE_TICKS);
                        animate(lid, lidTransformUp(LID_OPEN_DEG, 4f), LEAVE_TICKS);
                        animate(beam, transform(0.85f, 0.75f, 0.42f, 0.01f, 14f, 0.01f, 0), 20);
                        animate(item, transform(0, 2.4f, 0, 1.2f, 1.2f, 1.2f, 0), 30);
                    } else {
                        setItem(item, result);
                        offered = result.copy(); offeredName = resultName;
                        phase = Phase.OFFER;
                        l.playSound(null, at, SoundEvents.PLAYER_LEVELUP, SoundSource.BLOCKS, 0.8f, 1.6f);
                        // offer: floats there and slowly sinks back over the offer window
                        animate(item, transform(0, 0.25f, 0, 0.85f, 0.85f, 0.85f, 0), OFFER_TICKS);
                    }
                }
            }
            case OFFER -> { if (ticks >= OFFER_TICKS) close(l); }
            case CLOSING -> { if (ticks >= CLOSE_TICKS) { phase = Phase.IDLE; ticks = 0; } }
            case LEAVING -> {
                if (ticks % 5 == 0) l.sendParticles(ParticleTypes.POOF, at.getX() + 1, at.getY() + 1 + ticks / 10.0, at.getZ() + 0.5, 6, 0.6, 0.3, 0.4, 0.02);
                if (ticks >= LEAVE_TICKS) {
                    l.sendParticles(ParticleTypes.CLOUD, at.getX() + 1, at.getY() + 4, at.getZ() + 0.5, 30, 0.8, 0.5, 0.5, 0.05);
                    l.playSound(null, at, SoundEvents.ENDERMAN_TELEPORT, SoundSource.BLOCKS, 1.5f, 0.6f);
                    discard();
                    phase = Phase.GONE; ticks = 0;
                }
            }
            case GONE -> {
                if (ticks >= 40) {
                    spot = (spot + 1 + (spots.size() > 2 ? (int) (Math.random() * (spots.size() - 1)) : 0)) % spots.size();
                    at = spots.get(spot);
                    build(l);
                    l.sendParticles(ParticleTypes.END_ROD, at.getX() + 1, at.getY() + 1, at.getZ() + 0.5, 25, 0.6, 0.6, 0.3, 0.05);
                    l.playSound(null, at, SoundEvents.BEACON_ACTIVATE, SoundSource.BLOCKS, 1.5f, 1.2f);
                }
            }
            default -> { }
        }
    }

    /** F while the gun floats: hand it over, close up. */
    ItemStack take(ServerLevel l) {
        if (phase != Phase.OFFER) return ItemStack.EMPTY;
        ItemStack r = offered; offered = ItemStack.EMPTY;
        close(l);
        return r;
    }

    void close(ServerLevel l) {
        if (item != null) { item.discard(); item = null; }
        animate(lid, lidTransform(0f), CLOSE_TICKS);
        l.playSound(null, at, SoundEvents.CHEST_CLOSE, SoundSource.BLOCKS, 1f, 0.6f);
        phase = Phase.CLOSING; ticks = 0; offered = ItemStack.EMPTY;
    }

    // ------------------------------------------------------------------ display helpers
    static CompoundTag transform(float tx, float ty, float tz, float sx, float sy, float sz, float yawDeg) {
        CompoundTag t = new CompoundTag();
        t.put("translation", vec(tx, ty, tz));
        t.put("left_rotation", quat(0, 1, 0, yawDeg));
        t.put("scale", vec(sx, sy, sz));
        t.put("right_rotation", quat(0, 1, 0, 0));
        return t;
    }

    /** Lid hinged on its back-bottom edge (z = 13/16), sitting on top of the 11/16-tall body. */
    static CompoundTag lidTransform(float openDeg) { return lidTransformUp(openDeg, 0f); }

    static CompoundTag lidTransformUp(float openDeg, float up) {
        double a = Math.toRadians(openDeg);
        float hz = 13f / 16f, hy = 0f;
        // translation = h - R*h + offset, R = rotation about +X by a (front edge rises)
        float ry = (float) (hy * Math.cos(a) - hz * Math.sin(a)), rz = (float) (hy * Math.sin(a) + hz * Math.cos(a));
        CompoundTag t = new CompoundTag();
        t.put("translation", vec(0, hy - ry + 11f / 16f + up, hz - rz));
        t.put("left_rotation", quat(1, 0, 0, openDeg));
        t.put("scale", vec(1, 1, 1));
        t.put("right_rotation", quat(0, 1, 0, 0));
        return t;
    }

    static ListTag vec(float x, float y, float z) { ListTag l = new ListTag(); l.add(FloatTag.valueOf(x)); l.add(FloatTag.valueOf(y)); l.add(FloatTag.valueOf(z)); return l; }

    static ListTag quat(float ax, float ay, float az, float deg) {
        double h = Math.toRadians(deg) / 2, s = Math.sin(h);
        ListTag l = new ListTag();
        l.add(FloatTag.valueOf((float) (ax * s))); l.add(FloatTag.valueOf((float) (ay * s))); l.add(FloatTag.valueOf((float) (az * s))); l.add(FloatTag.valueOf((float) Math.cos(h)));
        return l;
    }

    static Display.BlockDisplay blockDisplay(ServerLevel l, BlockState state, Vec3 pos, CompoundTag transformation) {
        Display.BlockDisplay d = EntityType.BLOCK_DISPLAY.create(l);
        if (d == null) return null;
        CompoundTag t = new CompoundTag();
        t.put("block_state", NbtUtils.writeBlockState(state));
        t.put("transformation", transformation);
        t.putFloat("view_range", 2f);
        d.load(withPos(d, t, pos));
        l.addFreshEntity(d);
        return d;
    }

    static Display.ItemDisplay itemDisplay(ServerLevel l, ItemStack stack, Vec3 pos) {
        Display.ItemDisplay d = EntityType.ITEM_DISPLAY.create(l);
        if (d == null) return null;
        CompoundTag t = new CompoundTag();
        t.put("item", stack.save(new CompoundTag()));
        t.putString("item_display", "fixed");
        t.putString("billboard", "vertical");
        t.put("transformation", transform(0, 0, 0, 0.85f, 0.85f, 0.85f, 0));
        CompoundTag bright = new CompoundTag(); bright.putInt("sky", 15); bright.putInt("block", 12);
        t.put("brightness", bright);
        d.load(withPos(d, t, pos));
        l.addFreshEntity(d);
        return d;
    }

    static CompoundTag withPos(Entity e, CompoundTag display, Vec3 pos) {
        CompoundTag t = e.saveWithoutId(new CompoundTag());
        t.merge(display);
        ListTag p = new ListTag();
        p.add(net.minecraft.nbt.DoubleTag.valueOf(pos.x)); p.add(net.minecraft.nbt.DoubleTag.valueOf(pos.y)); p.add(net.minecraft.nbt.DoubleTag.valueOf(pos.z));
        t.put("Pos", p);
        return t;
    }

    /** Same path as /data merge entity: save, merge, load (UUID kept). */
    static void merge(Entity e, CompoundTag patch) {
        if (e == null || e.isRemoved()) return;
        CompoundTag t = e.saveWithoutId(new CompoundTag());
        java.util.UUID id = e.getUUID();
        t.merge(patch);
        e.load(t);
        e.setUUID(id);
    }

    /** Interpolated transform change (client renders the motion smoothly). */
    static void animate(Entity e, CompoundTag transformation, int ticks) {
        CompoundTag p = new CompoundTag();
        p.put("transformation", transformation);
        p.putInt("interpolation_duration", ticks);
        p.putInt("start_interpolation", 0);
        merge(e, p);
    }

    static void setItem(Display.ItemDisplay d, ItemStack s) {
        if (d == null || s.isEmpty()) return;
        CompoundTag p = new CompoundTag(); p.put("item", s.save(new CompoundTag()));
        merge(d, p);
    }
}
