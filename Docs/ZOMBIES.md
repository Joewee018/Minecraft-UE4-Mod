# Zombies mode: enemy AI, animation, maps

This is an original implementation that aims for the feel of round-based Black Ops II Zombies. It contains no Activision or Treyarch code, assets or sounds.

## Architecture

**Authority.** Java (Minecraft 1.20.1 on Fabric) is authoritative. It runs the AI, damage, death, rounds and points. Unreal only draws what Java exports and sends input. Nothing runs on the Unreal side that could disagree with the server.

| Piece | File |
|---|---|
| Tuning (one place) | `Bridge/src/main/java/crb/zm/ZmConfig.java` |
| AI state machine, combat, crowd spacing | `Bridge/src/main/java/crb/zm/ZmZombie.java` |
| Rounds, spawning, barricades, buys, power-ups, map mode | `Bridge/src/main/java/crb/zm/ZombiesGame.java` |
| Animation (blocky, per state) | `Bridge/src/client/java/crb/client/zm/ZmZombieModel.java` |
| Renderer (vanilla zombie texture and armor layers) | `Bridge/src/client/java/crb/client/zm/ZmZombieRenderer.java` |
| State export to Unreal (`zm.zombies[].anim` = state id) | `Bridge/src/client/java/crb/client/StateExporter.java` |
| Unreal HUD, F interact, R reload, menus | `CrbHUD.cpp`, `CrbZombies.cpp`, `CrbMenus.cpp` |
| Tests | `Unreal/Source/CrossoverRebuilt/CrbTestZombies.cpp` (`-CrbTest=zombies`) |

## AI states (`ZmZombie.ZState`; the ordinal is the exported `anim`)

| id | State | What happens |
|---|---|---|
| 0 | SPAWN | Rises in over 12 ticks, then IDLE. |
| 1 | IDLE | Outside a barricade it goes to BREACH. Otherwise it acquires the nearest valid player (alive, not creative or spectator, within `detectRange`) and starts CHASE after `reactionTicks`. |
| 2 | SEARCH | Lost line of sight for `loseSightTicks`: walks to the last known position, looks around, then resumes CHASE or gives up to IDLE. |
| 3 | CHASE | Vanilla pathfinding to the target, re-pathed every `repathTicks`. Speed rises by `closeBoost` inside `closeRange`. After `stuckTicks` without progress it drops the path and sidesteps. |
| 4 | BREACH | Walks to its window. |
| 5 | TEAR | Tears one board every `tearTicks`. With no boards left it walks in. After `breachGiveUpTicks` it respawns at its spawn point instead of looping. |
| 6 | WINDUP | Telegraph: stops and faces the target for `windupTicks`. |
| 7 | STRIKE | The only frames that can deal damage (`doHurtTarget` is refused in every other state). The hit needs the target within `attackReach + 0.4`. |
| 8 | RECOVER | `recoverTicks` of recovery, then `attackCooldownTicks` before the next attack. |
| 9 | STAGGER | A hit of at least `staggerMinDamage` cancels the current attack or move, knocks the zombie back by `knockback`, pauses for `staggerTicks`, then resumes. A re-hit restarts the stagger. |
| 10 | DEATH | Terminal: navigation stopped, target cleared, no attacks. Vanilla death fall, removed after 20 ticks. |

**Rules that prevent conflicting actions:**
- One goal (`Brain`) holds MOVE, LOOK and JUMP.
- `setState` is the only transition, and nothing leaves DEATH.
- The zombie cannot attack while dead, staggered or winding up.
- Chase poses come from measured movement (`limbSwingAmount`), so a stationary zombie never plays a run.

**Crowd spacing.** Every tick each zombie is pushed away from other zombies inside `separationRadius`.

**Physics.** Vanilla collision, gravity, step-up and knockback; nothing is teleported except the unstick and give-up recoveries.

## Animation (`ZmZombieModel`)
Vanilla walk and zombie arms come first, then a per-state pose on top:

- **Spawn:** climb in.
- **Idle and search:** arm sway; while searching, the head scans.
- **Walk and run:** a bigger stride, forward lean and arm bob as speed rises.
- **Turning:** the body follows the head.
- **Tear:** alternating two-arm board yanks timed to `tearTicks`.
- **Attack:** wind-up (arms up and back, lean back), then strike (slam down and forward), then recover.
- **Stagger:** snap back with arms flung out, decaying.
- **Death:** limp arms with the vanilla fall.

Unreal shows these through the existing Java pose export.

**Swapping in a skinned model later:** drive an Unreal anim instance from `zm.zombies[].anim` and actual velocity, as the God of War Mutant does (`FCrbPlayerAvatar` enemy pool). Then hide the pose-exported stand-in with a `NoopRenderer`.

## Adding zombies to a level
- **Superflat arena:** Esc → Game Modes... → Zombies. The arena is built at (96, 96).
- **Imported map:** Esc → Maps... → pick a map (for example Nuketown), then Esc → Game Modes... → Zombies. No arena is built: zombies spawn on the map's own ground 16–30 blocks around you, and a weapon crate is placed in front of you.
- **Debug (dev worlds):** `zm.debug` accepts:
  - `{"spawnZombie": dist}` spawns a zombie that is already inside.
  - `{"hurtZombie": dmg}`, `{"killAll": true}`, `{"hold": true}` (pause between rounds), `{"powerUp": true}`, `{"round": n}`, `{"points": n}`.

## Important tuning (`ZmConfig`)
| Group | Settings |
|---|---|
| Perception | `detectRange`, `loseSightTicks`, `searchGiveUpTicks`, `reactionTicks` |
| Movement | `walkSpeed`, `runSpeed`, `sprintSpeed` (the runner share grows from round 4), `closeBoost`, `closeRange`, `separationRadius`, `separationPush`, `repathTicks`, `stuckTicks` |
| Attack | `attackReach`, `windupTicks`, `activeTicks`, `recoverTicks`, `attackCooldownTicks`, `attackDamage` |
| Barricades | `tearTicks`, `breachGiveUpTicks` |
| Hit reaction | `staggerTicks`, `staggerMinDamage`, `knockback` |

## Missing assets and limitations
- The zombie uses the vanilla Minecraft zombie model as its placeholder (by choice). A Mixamo-skinned zombie can replace it later through the anim contract above.
- There's no ragdoll. Minecraft has no ragdoll physics, so death is the vanilla fall plus the death pose.
- Multiplayer: everything is server-side. Remote players would see the same synced state through vanilla entity tracking, but there's no dedicated-server host mode yet.
- Guns++ is All Rights Reserved and used only in the private dev client.
  - Guns are its charged-crossbow form. The `gz_bullets` score is set first, because Guns++ only yields that form with bullets loaded.
  - Empty guns reload by themselves; R also reloads.

## Manual checklist
1. **Targeting:** start Zombies. Zombies leave SPAWN, tear a window (boards disappear one by one), climb in and turn toward you.
2. **Chasing:** walk away around the room. They follow around corners, don't stack into one spot, and speed up when close. Break line of sight for 3 s: they walk to where you were, look around, then resume.
3. **Attacks:** stand still. Each attack visibly raises its arms first (wind-up). Damage lands only on the slam, and there is a pause before the next attack.
4. **Barricades:** hold F at a window to rebuild. They tear the boards again instead of getting stuck, and a zombie stuck too long respawns outside.
5. **Hit reactions:** shoot or hit a zombie. It flinches back and pauses (its attack is cancelled), then continues.
6. **Death:** kill one. It stops moving and attacking, falls over and disappears, and points are awarded once.
