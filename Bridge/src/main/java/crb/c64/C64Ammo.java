package crb.c64;

/** Craft 64 ammo pools (Doom capacities; a backpack doubles them). */
public enum C64Ammo {
    NONE("none", "", 0, 0),
    BULLETS("bullets", "Bullets", 200, 10),
    SHELLS("shells", "Shells", 50, 4),
    ROCKETS("rockets", "Rockets", 50, 1),
    CELLS("cells", "Cells", 300, 20);

    public final String key, display; public final int max, clip;
    C64Ammo(String key, String display, int max, int clip) { this.key = key; this.display = display; this.max = max; this.clip = clip; }
}
