# Crossover-Rebuilt - status (2026-10-07)

Project: `C:\Users\joewe\Documents\Codex\2026-10-04\act-as-an-expert-unreal-engine\outputs\Crossover-Rebuilt` (the old `outputs\Crossover` is read-only reference).
Play: desktop shortcut "Crossover-Rebuilt (Play)", or `Play.cmd` / `PlayDebugWorld.cmd`.
Tools: `Tools\BuildBridge.ps1`, `Tools\PPBuild.ps1` (Steve rig + P_/E_/R_ clips -> Work\pp\build), `Tools\BuildUnreal.ps1 [-SkipAssets]`, `Tools\TestUnreal.ps1 -Suite <boot|ec|ecring|pp|sm64|craft64|avatar|zombies|all>`.
(`Jobs\` + `Crossover-Rebuilt-Run.cmd` were Claude's remote job pipeline - don't use them; delete any `Jobs\sync-*.zip`.)

## Architecture
- Java authoritative (Minecraft 1.20.1 Fabric, `Bridge/`); Unreal presents (UE 4.27 C++, `Unreal/`).
- Mods menu (M): `CrbMenus::OpenModMenu`, `mod:<id>` rows, `UPROPERTY(Config)` toggles on ACrbHost; one player-character mod at a time (avatar / sm64 / craft64 / physicsportal / eldencombat).
- F4 debug menu; a mod's debug section appears only while it is on.
- HUD textures via `TextureExporter.request` (Java client) / `Textures.Get` (Unreal); Minecraft font glyph mapping `CrbMcUi::Glyph()`.
- Unity build: unique names for anonymous-namespace helpers. Non-ASCII text in TEXT() as \u escapes.

## Done / verified
- Full suite earlier 121/121; God of War Unity mod 37/37; Guns++, glass shatter, Maps menu, Zombies 48/50, menu animations.
- SM64 Steve Movement 44/44; Craft 64 49/49. Skate mod removed (user request).
- Load check: `-Suite boot` 3/3.
- Minecraft x Elden Combat (`mod:eldencombat`, id MinecraftEldenCombat): `-Suite ec` 47/47.
  - Java `crb/ec/EldenCombat.java`, `ECWeapon.java`, `mixin/PlayerECDamageMixin.java`, client `crb/client/ec/ECClient.java`; HostInput "ec" block; ServerOps `ec.*` ops (debug ops dev-world only, all refused while off).
  - Unreal `CrbEldenCombat.cpp`, `CrbECSteve.*`, HUD `DrawEldenCombat`, `OpenECMenu`, `CrbTestEC.cpp`; assets `Unreal/Scripts/CreateEC.py` -> /Game/Crb/EC; clips in `Avatar/blender/pp_steve_build.py`.
- Elden Ring Combat (Steve) (`mod:eldenring`, ECStyle = 1): `-Suite ecring` 45/47; the 2 fails were low stamina before guard/parry (test now refills stamina) - re-run pending, plus larger HUD text and wider blade (in the source, not yet built).

## Paused: Physics & Portal (`mod:physicsportal`), pp3 32/35
- Portal momentum test: friction slows Steve before portal A (teleport closer / airborne).
- Floor-portal test: crossed during the settle step (raw test.tp).
- Ramp test: rolling = ROLL state; verify the new PPController hop().
- `M_Portal` fails to compile for SM5 - read the CreatePP.py editor log.

## Queued
- Cel-shading toggle mod (post-process only, lighting untouched). Peach's Castle exterior map for SM64.
- Re-run `-Suite all` to confirm the HUD texture guard fixed the crash after the reconnect test.
