package crb.ec;

import net.minecraft.world.item.*;

/**
 * Minecraft x Elden Combat - weapon classes for the Minecraft items the mod treats as weapons. Damage always starts from
 * the item's own vanilla attack damage (attribute, enchantments included); the class only sets the move set: timing in
 * ticks (windup, active, recovery), reach, swing arc, motion values, stamina cost and poise damage.
 */
public enum ECWeapon {
    //            key        reach arc  lightW lightA lightR  heavyW heavyA heavyR  lightMV heavyMV stam  poise combo thrust
    FIST       ("fist",       2.6, 70,   3, 2, 5,      7, 2, 9,      1.00, 1.60,  9,   6,   3, false),
    SWORD      ("sword",      3.2, 120,  4, 3, 7,      9, 3, 11,     1.00, 1.55, 14,  10,   3, false),
    AXE        ("axe",        3.3, 110,  6, 3, 9,     12, 3, 13,     1.05, 1.70, 18,  16,   2, false),
    HAMMER     ("hammer",     3.0, 100,  8, 3, 11,    15, 4, 15,     1.10, 1.90, 24,  26,   2, false),
    SPEAR      ("spear",      4.4, 30,   5, 3, 7,     10, 3, 11,     1.00, 1.50, 14,  10,   3, true),
    SCYTHE     ("scythe",     3.6, 160,  6, 3, 9,     11, 4, 12,     0.95, 1.55, 16,  10,   2, false),
    CLUB       ("club",       3.0, 100,  5, 3, 8,     10, 3, 12,     1.00, 1.60, 15,  14,   2, false);

    public final String key; public final double reach, arc; public final int lw, la, lr, hw, ha, hr; public final double lmv, hmv;
    public final int stamina, poise, combo; public final boolean thrust;
    ECWeapon(String key, double reach, double arc, int lw, int la, int lr, int hw, int ha, int hr, double lmv, double hmv, int stamina, int poise, int combo, boolean thrust) {
        this.key = key; this.reach = reach; this.arc = arc; this.lw = lw; this.la = la; this.lr = lr; this.hw = hw; this.ha = ha; this.hr = hr;
        this.lmv = lmv; this.hmv = hmv; this.stamina = stamina; this.poise = poise; this.combo = combo; this.thrust = thrust;
    }

    public static ECWeapon of(ItemStack s) {
        if (s == null || s.isEmpty()) return FIST;
        Item i = s.getItem();
        if (i instanceof SwordItem) return SWORD;
        if (i instanceof AxeItem) return AXE;
        if (i instanceof PickaxeItem) return HAMMER;
        if (i instanceof TridentItem) return SPEAR;
        if (i instanceof HoeItem) return SCYTHE;
        if (i instanceof ShovelItem) return CLUB;
        return FIST;
    }

    public static ECWeapon byKey(String k) { for (ECWeapon w : values()) if (w.key.equals(k)) return w; return null; }
}
