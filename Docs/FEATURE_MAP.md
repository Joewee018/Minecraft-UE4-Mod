# Feature map (from the read-only reference `outputs\Crossover`) and staged checklist

## What the reference build does (v0.10.0, per its README/source; its reports are NOT current evidence)
- Loopback bridge (JSON frames, protocol 9) between a Fabric 1.20.1 dev client (Yarn) and a UE4.27 host.
- Java-authoritative movement/inventory/crafting; Unreal pawn presents positions; HUD/inventory in UMG.
- Player body/hands captured from Java renderers; lighting from the Java light map; particles; gravity gun (G);
  orbital railgun via a third-party mod jar; F4 debug-test menu with "All Features" fixture.

## Failures found in the reference (root-cause analysis, read-only)
1. **Movement/animation freeze after "All Features"**: `CrossoverAvatar.cpp` `ApplyPose()` returns early unless
   *every* surface texture already has a material (`if(!Materials.Contains(S.Asset)) return;`). "All Features" equips
   armor and a held item; held items render from the block atlas whose PNG exceeds the 600 KB player-texture budget, so
   that texture is rejected and never arrives -> every later pose is discarded while input keeps working. Old debug-menu
   report still says "passed" because its check did not assert that *new* poses reached the renderer.
2. **Plain grey/brown first-person shovel with a correct hotbar icon**: same mechanism - the item surface referenced
   the rejected/never-sent block atlas, so it rendered with an untextured fallback (hotbar icons came from a separate path).
3. **Dark, flat lighting** (user screenshot): lightmap only fed a small emissive term (`MinecraftLightResponse 0.14`,
   floor 0.07) on top of a weak 1000-lux sun with auto-exposure; block faces never received vanilla's
   `atlas x tint x shade x lightmap` brightness.
4. Gradle runs and Minecraft start scripts wrote reports/caches into the project folder; tests reused old reports.

## How Crossover-Rebuilt avoids them
- Pose geometry is applied every frame regardless of texture state; missing/failed textures use a vertex-colour fallback
  material and are re-bound when they arrive (`CrbAvatar.cpp`). Textures travel on an independent chunked lane
  (192 KB chunks, 16 MB cap) so the 1024x512 block atlas always arrives; a failure is counted and logged only.
- Regression assertions: after every fixture on/off and after Resume, Java position must move > 2 blocks, >= 20 new Java
  pose frames must reach Unreal, gait must advance, and >= 10 distinct body vertex sets must be handed to the renderer.
- Held items: real `ItemInHandRenderer`/`PlayerRenderer` output with block-atlas UVs; test asserts no fallback surfaces.
- Lighting: vanilla terrain math in the material (Minecraft LightTexture sampled at per-vertex (block, sky)), manual
  exposure normalised to the editable 10000-lux sun; UE sun/contact shadows are a weighted extra layer.

## Staged checklist
- [x] Stage 0 - read-only inventory, version record, new project skeleton, job runner inside Crossover-Rebuilt only.
- [x] Stage 1 - Fabric bridge: bounded endpoint, input on render thread, server ops on server thread, state, pose,
      sections/models, textures, lightmap, particles, fixtures, gravity gun, orbital strike; unit + live Java tests.
- [x] Stage 2 - UE host compile, materials/map via Python commandlet, packaged Win64 game.
- [x] Stage 3 - packaged end-to-end suite + screenshots, fix failures, repeat.
- [x] Stage 4 - game UI: Esc pause menu (Back to Game / Mods / Quit Game - saves the world and closes Minecraft),
      death screen with Respawn, survival/creative switch in the debug menu, creative flight, vanilla creative
      inventory (tabs, pages, scroll, search, carried stack, Survival Inventory tab, Destroy Item), creative/spectator
      HUD rules, Java titles (custom fonts included) and HUD chat. Orbital Strike Cannon removed.
- [x] Stage 5 - world entities (mobs, items, armor stands) through the real EntityRenderDispatcher; Herobrine via the
      third-party From The Fog mod (dev mods folder only) with an in-game settings menu driving the mod's own config.
- [ ] Later - block entities (chests, signs), player model in the inventory preview, multiplayer server mode.

## Stage 4/5 design notes
- Creative inventory: tab contents come from `CreativeModeTabs` rebuilt with the same feature flags/permissions as
  `CreativeModeInventoryScreen`; the carried stack lives in the player's `InventoryMenu` (client side, like vanilla);
  every slot change is sent with `MultiPlayerGameMode.handleCreativeModeItemAdd` (`ServerboundSetCreativeModeSlotPacket`),
  which the server only accepts in creative. Icons for the current page/tabs are rendered by Minecraft's own GUI item
  renderer into `crb:creative_page` / `crb:creative_tabs`.
- Titles: Java draws `Gui.title`/`subtitle` exactly like `Gui.render` (Minecraft Font, scale 4/2) into an offscreen
  target published as `crb:title`; Unreal applies the vanilla fade alpha. This is how From The Fog's custom-font
  jumpscare titles appear in Unreal.
- Entities: `PoseExporter` group 2 = up to 48 entities within 64 blocks rendered by `EntityRenderDispatcher.render`
  into the capture buffer (feet-relative); shadows/name tags/glints are skipped. Bounded by the 36000-vertex pose cap.
- Herobrine control (`crb.Herobrine`): settings run the mod's own `watching:config/<key>/<value>` functions (what its
  chat buttons run), values are read from its `ftf.configOptions` scoreboard, sightings use its
  `fromthefog:admin/...` functions. The mod jar is pinned by Modrinth URL + SHA-512 in `Tools\InstallMods.ps1` and is
  never copied into `Package\`.
