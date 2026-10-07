# God of War Unity mod (port of iltenahmet/god-of-war-unity)

This mod ports the MIT-licensed Unity project [iltenahmet/god-of-war-unity](https://github.com/iltenahmet/god-of-war-unity) into Crossover-Rebuilt so it is playable on top of real Minecraft 1.20.1.

Toggle it in the Mods menu (M) with **God of War Unity mod** (row key `mod:avatar`). The setting is saved as `bAvatarEnabled`. The same menu has **Spawn Mutant enemy** and **Remove Mutants**.

## What is ported

### Controls and movement (`PlayerController.cs`)

- **Movement:** strafe-style. The body turns to the camera yaw while moving or attacking and keeps its yaw when idle. There is no movement while the melee clip plays.
- **Melee (LMB, `Attack1`):** plays "Standing Melee Attack Downward" at state speed 2.
  - At the swing's hit point, Java op `gow.melee` deals **30 damage** to living entities in a 2.6-block, 70° arc. Each target has a 1 s cooldown.
  - Melee only damages while the axe is in hand.
- **Axe throw (RMB, empty main hand):** plays the throw one-shot at state speed 1.8 and releases at the repo's animation-event fraction (0.733 / 2.267).
  - The direction is a ray from the camera centre to the first solid block or Mutant (`SetAxeThrowDirection`).
  - Java `AxeEntity` flies at **7 m/s with no gravity** (`axeThrowSpeed` 7, Rigidbody `useGravity` 0) and sticks to whatever it hits, including riding along inside an enemy (`StickTo`).
  - It deals **30 damage** to anything with health it touches while detached, with a 1 s cooldown.
- **Recall (R or middle mouse, `Fire3`):** a quadratic Bézier back to the hand over **1 s** (`axeRecallTime`). Recall works at any time, including mid-flight, and the axe also deals damage on the way back.

### Health (`Health.cs`, `Billboard.cs`)

- The Mutant has **100 health**.
- A floating health bar is drawn over each Mutant.
- At 0 health the Mutant plays its Death clip and is removed after 2 s.

### Animation (`PlayerAnimationManager.cs`, `PlayerAnimator.controller`, Mutant Animator)

- The 2D movement blend tree of Idle plus Walk F/B/L/R is driven by the local move direction, smoothed with `MoveTowards` at 10/s. Sprinting uses Run F/B and Jog Strafe L/R.
- The enemy uses Mutant Idle and Mutant Dying.
- Everything runs through a native C++ anim instance (`CrbAvatarAnim.*`); there are no Blueprints.

### UI

- The repo's translucent square reticle is shown.
- Minecraft's hotbar and vitals are hidden while the mod is on.
- Inventory, creative, F4, pause, death/respawn, chat, titles, flight, Herobrine and other mobs all keep working.

## Private build: Kratos and the Leviathan Axe

**The player is the repo's Kratos model** (`Assets/Kratos/KRATOS FORTNITE.fbx` with its body, head and beard textures). **The axe is the repo's Leviathan Axe** (`Assets/Axe`, four PBR materials, with textures downsized to 2048).

The repo's README links both from Sketchfab. They are a Fortnite rip of Sony's character and a fan model of a Sony design, and the repo's MIT license does not cover them. The user asked for them in a **private, never-distributed build**, so do not share this build or its pak.

**How Kratos is animated:** Kratos uses Epic's Fortnite skeleton (root, pelvis, spine_01…05, upperarm_l, and so on), while the clips use the Mixamo skeleton. `gow_build.py` retargets the clips:

- A Mixamo→Epic bone name map.
- Rest-direction alignment, because the two rigs rest in different poses.
- World-space rotation deltas.
- Pelvis travel scaled by hip height.
- The Mutant's Dying clip is used as Kratos's death.

**The Mutant** stays the enemy, with its own Idle, Swiping and Dying clips.

**`Axe Throw.anim`** is Unity Humanoid muscle data and can't be used outside Unity. The throw reuses the overhand "Standing Melee Attack Downward" swing at the throw's state speed and release timing. The Nature Starter Kit environment is not used; Minecraft is the environment.

## Asset build

| Step | Script | Result |
|---|---|---|
| 1 | `Tools/AvatarProbe.ps1` | The repo zip (curl), filtered extraction |
| 2 | `Tools/AvatarBuild.ps1` → `Avatar/blender/gow_build.py` (Blender 3.6.23, SHA-256 pinned by `Tools/InstallBlender.ps1`) | `fbx/Avatar.fbx` (Kratos, 1.9 m), `fbx/Mutant.fbx` (2.05 m), `fbx/LeviathanAxe.fbx` (re-framed: grip at the origin, haft +Z, blade +X, 0.8 m), `fbx/A_<Role>.fbx` (11 player clips retargeted to the Epic skeleton) and `fbx/E_<Role>.fbx` (3 Mutant clips). Also writes `skeleton.json` (bone CRC), `clips.json`, previews and pose previews. |
| 3 | `Tools/BuildUnreal.ps1` → `Unreal/Scripts/CreateAvatar.py` | `/Game/Crb/Avatar`: `SK_Avatar` (Kratos), `SK_Mutant`, `SM_LeviathanAxe`, `A_*`, `E_*`, the textures and one material per slot |

The Mixamo animations and the Mutant character are used inside the cooked game only, as Mixamo's terms allow for games.

## Tests

The tests live in `CrbTestAvatar.cpp` and run with `-CrbTest=avatar` or as part of `all`:

- **Rig:** the skeleton CRC matches the Blender build; 13/13 clips are loaded.
- **Menu toggle:** turning the mod on hides vanilla pose groups 0/1, the hotbar and vitals, and shows the reticle; the model is about 190 UU tall; the orbit camera works. Screenshots `30`–`32`.
- **Movement:** W walks toward the camera yaw (Walk Forward blend); D strafes right while still facing the camera (Walk Right blend); sprint uses the Run set. Screenshots `33`–`34`.
- **Spawn Mutant row:** the enemy appears with the Mutant mesh in Idle and a health bar. Screenshot `35`.
- **Throw:** RMB → the throw clip → the axe sticks in the Mutant (health ≤ 70). Screenshots `36`–`37`.
- **Recall:** R brings the axe back to the hand. Screenshot `38`.
- **Melee:** LMB swings kill the Mutant (30 per hit); Mutant Dying plays and the body is removed. Screenshots `39`–`40`.
- **Other features with the mod on:** inventory, pause, creative flight, and player death (Mutant Dying) with respawn. Screenshots `41`–`42`.
- **Toggle off:** vanilla is restored. Screenshot `43`; a movement check follows.
