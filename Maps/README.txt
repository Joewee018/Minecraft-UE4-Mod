Custom maps for the in-game Maps menu (Esc > Maps...).

Put here either:
  - a Minecraft world folder (any folder that contains level.dat, nested up to 5 levels), or
  - a .zip containing one (e.g. a CurseForge modpack zip such as "GTA V Craft": its overrides/saves/<world> is used).

On first load the world is copied to Work\mc\saves\crb-map-<name> (your original here is never modified),
world generation is turned off (only the map's saved chunks exist; everything beyond is void) and Minecraft
1.20.1 upgrades older saves on the fly. Blocks from Forge mods that are not installed become air.
Private build: map files are the authors' property; do not redistribute.
