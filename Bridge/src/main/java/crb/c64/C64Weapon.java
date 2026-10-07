package crb.c64;

/**
 * Craft 64 arsenal (Doom 64 line-up, rebuilt from Minecraft materials). Numbers follow the classic Doom engine behaviour
 * (35 Hz tics converted to 20 Hz server ticks; damage rolls as in Doom: N x (1..k)). Range and speed in blocks.
 */
public enum C64Weapon {
    //            key            name               slot ammo        use refire hit    pellets roll  mult spreadH spreadV range  projectile speed
    FIST       ("fist",        "Fist",             1, C64Ammo.NONE,    0, 8,  Hit.MELEE,   1, 10, 2,  0,    0,    2.2, null, 0),
    CHAINSAW   ("chainsaw",    "Chainsaw",         1, C64Ammo.NONE,    0, 2,  Hit.MELEE,   1, 10, 2,  0,    0,    2.0, null, 0),
    PISTOL     ("pistol",      "Pistol",           2, C64Ammo.BULLETS, 1, 8,  Hit.SCAN,    1, 3,  5,  1.2,  0,    64,  null, 0),
    SHOTGUN    ("shotgun",     "Shotgun",          3, C64Ammo.SHELLS,  1, 21, Hit.SCAN,    7, 3,  5,  5.6,  0,    48,  null, 0),
    SUPER      ("super",       "Super Shotgun",    3, C64Ammo.SHELLS,  2, 32, Hit.SCAN,    20, 3, 5,  11.2, 7.1,  40,  null, 0),
    CHAINGUN   ("chaingun",    "Chaingun",         4, C64Ammo.BULLETS, 1, 2,  Hit.SCAN,    1, 3,  5,  3.0,  0,    64,  null, 0),
    ROCKET     ("rocket",      "Rocket Launcher",  5, C64Ammo.ROCKETS, 1, 12, Hit.PROJ,    1, 8,  20, 0,    0,    96,  "rocket", 1.1),
    PLASMA     ("plasma",      "Plasma Rifle",     6, C64Ammo.CELLS,   1, 2,  Hit.PROJ,    1, 8,  5,  0,    0,    96,  "plasma", 1.4),
    BFG        ("bfg",         "BFG 9000",         7, C64Ammo.CELLS,  40, 34, Hit.PROJ,    1, 8, 100, 0,    0,    96,  "bfg", 1.3),
    UNMAKER    ("unmaker",     "Unmaker",          8, C64Ammo.CELLS,   1, 3,  Hit.BEAM,    1, 8,  10, 0,    0,    64,  null, 0);

    public enum Hit { MELEE, SCAN, PROJ, BEAM }

    public final String key, display; public final int slot; public final C64Ammo ammo; public final int use, refire;
    public final Hit hit; public final int pellets, roll, mult; public final double spreadH, spreadV, range; public final String projectile; public final double speed;

    C64Weapon(String key, String display, int slot, C64Ammo ammo, int use, int refire, Hit hit, int pellets, int roll, int mult,
              double spreadH, double spreadV, double range, String projectile, double speed) {
        this.key = key; this.display = display; this.slot = slot; this.ammo = ammo; this.use = use; this.refire = refire; this.hit = hit;
        this.pellets = pellets; this.roll = roll; this.mult = mult; this.spreadH = spreadH; this.spreadV = spreadV; this.range = range;
        this.projectile = projectile; this.speed = speed;
    }

    /** Doom-style damage roll: mult x (1..roll). */
    public int roll(java.util.Random r) { return mult * (1 + r.nextInt(roll)); }

    public static C64Weapon byKey(String k) { for (C64Weapon w : values()) if (w.key.equalsIgnoreCase(k)) return w; return null; }
}
