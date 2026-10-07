package crb.client.pp;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.util.Mth;

import java.util.LinkedHashMap;
import java.util.Map;

/** Physics & Portal mod tunables (debug menu / ops) and the Gish-style physics modifiers. Client thread. */
public final class PPConfig {
    private PPConfig() { }

    public static final class Tunable {
        public final String key, label; public final double def, min, max; public double value;
        Tunable(String key, String label, double def, double min, double max) { this.key = key; this.label = label; this.def = def; this.min = min; this.max = max; value = def; }
    }
    public static final Map<String, Tunable> T = new LinkedHashMap<>();
    private static Tunable t(String k, String l, double d, double lo, double hi) { Tunable x = new Tunable(k, l, d, lo, hi); T.put(k, x); return x; }

    public static final Tunable ACCEL = t("accel", "Acceleration (m/s2)", 14, 1, 60);
    public static final Tunable ROLL_ACCEL = t("rollAccel", "Rolling acceleration (m/s2)", 18, 1, 80);
    public static final Tunable FRICTION = t("friction", "Friction multiplier", 1, 0, 5);
    public static final Tunable AIR = t("airControl", "Air control (m/s2)", 5, 0, 40);
    public static final Tunable GRAVITY = t("gravity", "Gravity (m/s2)", 32, 2, 80);
    public static final Tunable LAUNCH = t("launch", "Jump / launch force (m/s)", 9, 1, 40);
    public static final Tunable WALK = t("walkSpeed", "Walk speed (m/s)", 4.3, 1, 15);
    public static final Tunable RUN = t("runSpeed", "Run speed (m/s)", 7.2, 1, 25);
    public static final Tunable ROLL_MAX = t("rollSpeed", "Max rolling speed (m/s)", 24, 2, 80);
    public static final Tunable SLOPE = t("slope", "Slope influence", 1, 0, 4);
    public static final Tunable RAGDOLL_IMPACT = t("ragdollImpact", "Ragdoll impact speed (m/s)", 20, 5, 80);

    public static boolean rolling = true, momentum = true, ragdollOnImpact = true;
    public static double timeScale = 1; public static boolean frozen;

    /** Gish-inspired modifiers: multipliers applied on top of the surface table. */
    public enum Modifier {
        NORMAL(1, 1, 1, 1, 0, 1, false),
        STICKY(3.0, 0.8, 0.8, 1, 0, 1, true),
        SLIPPERY(0.12, 0.6, 1.4, 1, 0, 1, false),
        SQUISHY(1, 1, 1, 1, 0.7, 1, false),
        HEAVY(0.7, 0.65, 1.3, 1.6, 0, 0.6, false),
        LIGHT(1, 1.1, 0.9, 0.5, 0.1, 2.2, false);
        public final double friction, accel, maxSpeed, gravity, bounce, air; public final boolean cling;
        Modifier(double f, double a, double m, double g, double b, double air, boolean cling) { friction = f; accel = a; maxSpeed = m; gravity = g; bounce = b; this.air = air; this.cling = cling; }
    }
    public static Modifier modifier = Modifier.NORMAL;

    public static double v(Tunable x) { return x.value; }

    public static void apply(JsonObject j) {
        for (Tunable x : T.values())
            if (j.has(x.key) && j.get(x.key).isJsonPrimitive() && j.get(x.key).getAsJsonPrimitive().isNumber()) x.value = Mth.clamp(j.get(x.key).getAsDouble(), x.min, x.max);
        if (j.has("rolling")) rolling = j.get("rolling").getAsBoolean();
        if (j.has("momentum")) momentum = j.get("momentum").getAsBoolean();
        if (j.has("ragdollOnImpact")) ragdollOnImpact = j.get("ragdollOnImpact").getAsBoolean();
        if (j.has("timeScale")) timeScale = Mth.clamp(j.get("timeScale").getAsDouble(), 0.05, 2);
        if (j.has("frozen")) frozen = j.get("frozen").getAsBoolean();
        if (j.has("modifier")) try { modifier = Modifier.valueOf(j.get("modifier").getAsString().toUpperCase()); } catch (IllegalArgumentException ignored) { }
    }

    public static void reset() { for (Tunable x : T.values()) x.value = x.def; rolling = momentum = ragdollOnImpact = true; timeScale = 1; frozen = false; modifier = Modifier.NORMAL; }

    public static JsonObject toJson() {
        JsonObject j = new JsonObject(); JsonArray a = new JsonArray();
        for (Tunable x : T.values()) {
            JsonObject o = new JsonObject(); o.addProperty("key", x.key); o.addProperty("label", x.label); o.addProperty("value", x.value);
            o.addProperty("def", x.def); o.addProperty("min", x.min); o.addProperty("max", x.max); a.add(o);
        }
        j.add("tunables", a);
        j.addProperty("rolling", rolling); j.addProperty("momentum", momentum); j.addProperty("ragdollOnImpact", ragdollOnImpact);
        j.addProperty("timeScale", timeScale); j.addProperty("frozen", frozen); j.addProperty("modifier", modifier.name());
        return j;
    }
}
