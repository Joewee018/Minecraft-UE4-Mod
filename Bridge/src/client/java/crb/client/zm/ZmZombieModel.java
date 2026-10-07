package crb.client.zm;

import crb.zm.ZmConfig;
import crb.zm.ZmZombie;
import crb.zm.ZmZombie.Variant;
import crb.zm.ZmZombie.ZState;
import net.minecraft.client.model.ZombieModel;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.util.Mth;

import java.util.Map;
import java.util.WeakHashMap;

/**
 * Blocky round-based-zombie animation (original keyframes in the style of BO2 Zombies) for the AI states, per movement
 * variant: a lurching asymmetric WALKER shamble, a hunched RUNNER reaching forward, a leaning arm-pumping SPRINTER and
 * a CRAWLER dragging itself on its hands. Attacks come in styles (two-arm overhead, right swipe, left swipe, crawler
 * lunge). Every state change cross-fades from the last rendered pose over BLEND ticks, so nothing snaps.
 * Unreal draws exactly this pose (Java pose export).
 */
public final class ZmZombieModel extends ZombieModel<ZmZombie> {
    static final float BLEND = 4f;            // ticks of cross-fade between states
    static final int N = 6 * 5;               // 6 parts x (xRot, yRot, zRot, y, z)

    /** Per-entity animation memory (client only): last rendered pose and the pose a blend starts from. */
    static final class Mem { final float[] last = new float[N], from = new float[N]; int stamp = Integer.MIN_VALUE; boolean has; }
    static final Map<ZmZombie, Mem> MEM = new WeakHashMap<>();

    final ModelPart[] parts;

    public ZmZombieModel(ModelPart root) {
        super(root);
        parts = new ModelPart[] { head, body, rightArm, leftArm, rightLeg, leftLeg };
    }

    static float ease(float x) { x = Mth.clamp(x, 0f, 1f); return x * x * (3f - 2f * x); }

    @Override
    public void setupAnim(ZmZombie e, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        super.setupAnim(e, limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch);
        ZState s = e.clientState;
        Variant v = e.variant();
        float t = Math.max(0f, ageInTicks - e.clientStateStart);
        pose(e, s, v, t, limbSwing, limbSwingAmount, ageInTicks, netHeadYaw);
        if (v == Variant.CRAWLER) crawlerBody(s, limbSwing, limbSwingAmount);

        // Cross-fade from the pose shown when the state (or variant) changed.
        Mem m = MEM.computeIfAbsent(e, k -> new Mem());
        int stamp = e.clientStateStart * 8 + v.ordinal();
        if (stamp != m.stamp) { if (m.has) System.arraycopy(m.last, 0, m.from, 0, N); m.stamp = stamp; }
        float k = m.has ? ease(t / BLEND) : 1f;
        float[] cur = new float[N];
        read(cur);
        if (k < 1f) { for (int i = 0; i < N; i++) cur[i] = Mth.lerp(k, m.from[i], cur[i]); write(cur); }
        System.arraycopy(cur, 0, m.last, 0, N); m.has = true;
        this.hat.copyFrom(this.head);
    }

    void read(float[] a) { int i = 0; for (ModelPart p : parts) { a[i++] = p.xRot; a[i++] = p.yRot; a[i++] = p.zRot; a[i++] = p.y; a[i++] = p.z; } }
    void write(float[] a) { int i = 0; for (ModelPart p : parts) { p.xRot = a[i++]; p.yRot = a[i++]; p.zRot = a[i++]; p.y = a[i++]; p.z = a[i++]; } }

    void pose(ZmZombie e, ZState s, Variant v, float t, float ls, float la, float age, float headYaw) {
        float sway = Mth.sin(age * 0.067f) * 0.05f;
        float side = (e.getId() & 1) == 0 ? 1f : -1f;                 // per-zombie handedness for asymmetric poses
        this.body.yRot = Mth.clamp(headYaw * Mth.DEG_TO_RAD * 0.25f, -0.35f, 0.35f); // body follows the head on turns
        switch (s) {
            case SPAWN -> {
                // Climbing in / rising: comes up from below with clawing arms.
                float k = 1f - ease(t / 12f);
                lift(14f * k);
                arms(-2.7f + 1.1f * (1 - k) + Mth.sin(t * 0.9f) * 0.25f, -2.7f + 1.1f * (1 - k) - Mth.sin(t * 0.9f) * 0.25f, 0.25f);
                this.body.xRot = 0.35f * k;
            }
            case IDLE -> {
                // Swaying in place, head lolling.
                arms(-1.0f + sway * 2, -0.8f - sway * 2, 0.08f);
                this.head.zRot = 0.18f * side + Mth.sin(age * 0.05f) * 0.08f;
                this.body.zRot = Mth.sin(age * 0.05f) * 0.04f;
                legs(0f);
            }
            case SEARCH -> {
                gait(v, ls, la, age, side);
                this.head.yRot += Mth.sin(age * 0.12f) * 0.7f;
            }
            case CHASE, BREACH -> gait(v, ls, la, age, side);
            case TEAR -> {
                // Yanking boards: alternating two-arm pulls, one board per tearTicks, body rocking with each pull.
                float ph = t / ZmConfig.tearTicks * Mth.TWO_PI;
                float pull = Mth.sin(ph);
                this.rightArm.xRot = -1.9f + pull * 0.75f;
                this.leftArm.xRot = -1.9f - pull * 0.75f;
                this.rightArm.zRot = -0.18f; this.leftArm.zRot = 0.18f;
                this.body.xRot = 0.18f + Math.abs(pull) * 0.12f;
                this.head.xRot = 0.15f;
                legs(0f);
            }
            case WINDUP, STRIKE, RECOVER -> attack(e, s, t, side);
            case STAGGER -> {
                // Hit reaction: jolted back and twisted to one side, arms thrown, decaying.
                float k = 1f - ease(t / ZmConfig.staggerTicks);
                this.body.xRot = -0.4f * k;
                this.body.yRot += 0.35f * k * side;
                this.head.xRot = -0.6f * k;
                this.head.zRot = 0.3f * k * side;
                this.rightArm.xRot = Mth.lerp(k, -1.4f, -0.4f); this.leftArm.xRot = Mth.lerp(k, -1.4f, -1.2f);
                this.rightArm.zRot = 1.0f * k; this.leftArm.zRot = -1.0f * k;
                this.rightLeg.xRot = 0.4f * k; this.leftLeg.xRot = -0.25f * k;
            }
            case DEATH -> {
                // Collapse: knees buckle and arms go limp while vanilla's death fall tips the body over.
                float k = ease(t / 8f);
                arms(Mth.lerp(k, -1.4f, -0.15f), Mth.lerp(k, -1.4f, 0.2f), 0.35f * k);
                this.head.xRot = 0.7f * k;
                this.head.zRot = 0.4f * k * side;
                this.rightLeg.xRot = -0.6f * k; this.leftLeg.xRot = 0.3f * k;
                lift(3f * k);
            }
        }
    }

    /** Locomotion per variant, driven by actual movement (limbSwing / limbSwingAmount), never by state alone. */
    void gait(Variant v, float ls, float la, float age, float side) {
        float c = Mth.cos(ls * 0.6662f), sn = Mth.sin(ls * 0.6662f);
        switch (v) {
            case WALKER -> {
                // Lurching shamble: one arm reaching, the other hanging; dragged leg; side-to-side lurch with each step.
                this.rightLeg.xRot = c * 1.1f * la;
                this.leftLeg.xRot = -c * 0.7f * la;                       // the dragging leg swings less
                this.leftLeg.zRot = -0.06f * side;
                this.body.zRot = sn * 0.14f * la;
                this.body.xRot = 0.12f + Math.abs(c) * 0.06f * la;
                this.head.zRot = 0.22f * side - sn * 0.08f * la;
                float reach = -1.55f + Mth.sin(age * 0.09f) * 0.07f, hang = -0.55f + c * 0.25f * la;
                if (side > 0) arms(reach, hang, 0.06f); else arms(hang, reach, 0.06f);
                lift(Math.abs(sn) * 0.8f * la);
            }
            case RUNNER -> {
                // Hunched jog, both arms reaching and swinging alternately, head pushed forward.
                this.rightLeg.xRot = c * 1.35f * la;
                this.leftLeg.xRot = -c * 1.35f * la;
                this.body.xRot = 0.38f;
                this.head.xRot = -0.35f;
                this.rightArm.xRot = -1.35f - c * 0.45f * la;
                this.leftArm.xRot = -1.35f + c * 0.45f * la;
                this.rightArm.zRot = 0.05f; this.leftArm.zRot = -0.05f;
                this.rightLeg.z = this.leftLeg.z = 2.2f;
                lift(Math.abs(c) * 0.9f * la);
            }
            case SPRINTER -> {
                // Full sprint: deep forward lean, arms pumping opposite the legs, long stride.
                this.rightLeg.xRot = c * 1.65f * la;
                this.leftLeg.xRot = -c * 1.65f * la;
                this.body.xRot = 0.58f;
                this.head.xRot = -0.55f;
                this.rightArm.xRot = -c * 1.25f * la - 0.35f;
                this.leftArm.xRot = c * 1.25f * la - 0.35f;
                this.rightArm.zRot = 0.12f; this.leftArm.zRot = -0.12f;
                this.rightLeg.z = this.leftLeg.z = 3.6f;
                lift(Math.abs(c) * 1.3f * la);
            }
            case CRAWLER -> {
                // Hand-over-hand pull (crawlerBody puts the torso on the ground).
                this.rightArm.xRot = -2.6f + c * 0.7f * la;
                this.leftArm.xRot = -2.6f - c * 0.7f * la;
                this.rightArm.zRot = 0.2f; this.leftArm.zRot = -0.2f;
            }
        }
    }

    void attack(ZmZombie e, ZState s, float t, float side) {
        int style = e.attackStyle();
        float wind = s == ZState.WINDUP ? ease(t / ZmConfig.windupTicks) : 1f;
        float hit = s == ZState.STRIKE ? ease(t / ZmConfig.activeTicks) : s == ZState.RECOVER ? 1f : 0f;
        float back = s == ZState.RECOVER ? ease(t / ZmConfig.recoverTicks) : 0f;
        legs(0f);
        switch (style) {
            case 0 -> { // two-arm overhead clubbing: up and back, then slam down
                float up = Mth.lerp(wind, -1.5f, -3.0f), down = Mth.lerp(hit, up, -0.5f), arm = Mth.lerp(back, down, -1.5f);
                arms(arm, arm, Mth.lerp(hit, 0.32f, 0.1f));
                this.body.xRot = Mth.lerp(back, Mth.lerp(hit, -0.25f * wind, 0.45f), 0.1f);
                this.head.xRot = -0.2f * wind * (1 - hit);
                this.rightLeg.xRot = -0.35f * hit * (1 - back);
            }
            case 1, 2 -> { // one-arm horizontal swipe (1 = right arm, 2 = left arm), body twisting into it
                boolean right = style == 1;
                ModelPart a = right ? this.rightArm : this.leftArm, o = right ? this.leftArm : this.rightArm;
                float sgn = right ? 1f : -1f;
                a.xRot = Mth.lerp(back, Mth.lerp(hit, Mth.lerp(wind, -1.5f, -1.9f), -1.25f), -1.5f);
                a.yRot = Mth.lerp(back, Mth.lerp(hit, 0.95f * sgn * wind, -0.75f * sgn), 0f);
                a.zRot = Mth.lerp(back, Mth.lerp(hit, -0.4f * sgn * wind, 0.15f * sgn), 0f);
                o.xRot = -1.2f; o.yRot = 0f; o.zRot = 0.1f * -sgn;
                this.body.yRot += Mth.lerp(back, Mth.lerp(hit, 0.35f * sgn * wind, -0.45f * sgn), 0f);
                this.body.xRot = 0.1f + 0.2f * hit * (1 - back);
            }
            default -> { // crawler lunge: rears up on its arms, then snaps forward
                float rear = wind * (1 - hit);
                this.head.xRot = Mth.lerp(hit, -0.9f * wind, 0.3f) * (1 - back);
                this.rightArm.xRot = this.leftArm.xRot = Mth.lerp(hit, -2.2f - 0.6f * rear, -2.9f);
            }
        }
    }

    /** Crawler: torso horizontal on the ground, legs trailing behind, everything lowered to floor level. */
    void crawlerBody(ZState s, float ls, float la) {
        if (s == ZState.DEATH) { lift(12f); return; }
        float c = Mth.cos(ls * 0.6662f);
        this.body.xRot = Mth.HALF_PI - 0.15f;
        this.head.xRot = Math.min(this.head.xRot, 0f) - 0.9f;   // looking up at the player
        this.rightLeg.xRot = Mth.HALF_PI - 0.1f + c * 0.15f * la; this.leftLeg.xRot = Mth.HALF_PI - 0.1f - c * 0.15f * la;
        this.rightLeg.y = this.leftLeg.y = 2f; this.rightLeg.z = this.leftLeg.z = 11f;
        this.rightLeg.zRot = 0.08f; this.leftLeg.zRot = -0.12f;
        this.head.y += 18f; this.body.y += 18f; this.rightArm.y += 18f; this.leftArm.y += 18f; this.rightLeg.y += 18f; this.leftLeg.y += 18f;
        this.head.z -= 1f; this.rightArm.z -= 1f; this.leftArm.z -= 1f;
    }

    void arms(float right, float left, float spread) {
        this.rightArm.xRot = right; this.leftArm.xRot = left;
        this.rightArm.yRot = -spread * 0.5f; this.leftArm.yRot = spread * 0.5f;
        this.rightArm.zRot = spread; this.leftArm.zRot = -spread;
    }

    void legs(float x) { this.rightLeg.xRot = x; this.leftLeg.xRot = -x; }

    void lift(float down) {
        this.head.y += down; this.body.y += down; this.rightArm.y += down; this.leftArm.y += down;
        this.rightLeg.y += down; this.leftLeg.y += down;
    }
}
