# Crossover-Rebuilt

An unofficial hybrid modding project that pairs a Java/Fabric Bridge with an Unreal Engine 4.27 presentation layer.

## Read this before downloading

This GitHub repository is a **source-code project, not a ready-to-play game or installer**.

It includes the Java/Fabric Bridge source for Minecraft world logic, the Unreal Engine C++ host source and project settings, build scripts, the baseline host map, and 10 baseline Unreal materials.

It does **not** include a packaged Windows game, a compiled Bridge jar, Minecraft game files, character meshes/skins/animations, map archives, or generated build output. As a result, downloading this repository alone will not let you play the game. You need the developer tools listed below and any required assets must be sourced or created with permission from their owners.

The project does not include Mojang/Microsoft code or assets, or other proprietary game content. Fabric Loom resolves mapped Minecraft build dependencies; users do not need to manually decompile Minecraft.

## Architecture

- `Bridge/` contains the Fabric mod source. Java owns world state, physics, combat, and AI.
- `Unreal/Source/` contains the UE4 C++ host. Unreal handles rendering, input, HUD, menus, characters, and camera presentation.
- `Unreal/Scripts/` contains editor-side asset/build scripts. The baseline host map and project materials are included; character/game-property assets are excluded.
- `Tools/` contains local build and test scripts.

## How to install / build from source

This is a **developer setup guide**, not a one-click player install. The repository does not contain a ready-to-run game package or all Unreal content assets. You do not need to manually decompile Minecraft or feed the project to an AI coder: Fabric Loom resolves the mapped Minecraft and Fabric dependencies during the Bridge build. You do need the required local tools and authorized game content.

### Requirements

- Windows and Git.
- Unreal Engine 4.27 with its C++ build tools.
- Java Development Kit (JDK) 17.
- An internet connection so Gradle can download build dependencies.
- Minecraft 1.20.1 and Fabric-compatible dependencies for local development. These are not included in this repository.
- Blender 3.6 only if you want to regenerate the optional character rigs and animation clips.

### Build it locally

1. Clone this repository:

   ```powershell
   git clone https://github.com/Joewee018/Minecraft-UE4-Mod.git
   cd Minecraft-UE4-Mod
   ```

2. Open `Tools/Common.ps1` and update `$env:JAVA_HOME` and `$UE` to the JDK 17 and Unreal Engine 4.27 folders on your computer. The checked-in values point to the original developer's machine.

3. Build the Java/Fabric Bridge:

   ```powershell
   .\Tools\BuildBridge.ps1
   ```

4. Build and package the Unreal host:

   ```powershell
   .\Tools\BuildUnreal.ps1
   ```

   The generated package is written under `Package\`. Builds may require locally sourced Unreal content that is intentionally excluded from this public repository.

5. For development testing, run the available test suite on a disposable superflat world. For example:

   ```powershell
   .\Tools\TestUnreal.ps1 -Suite boot
   ```

The build scripts do not install Minecraft or grant a game license. This repository is source code and project configuration; it is not currently a verified, mod-free base download or an end-user installer.

## Rights and use

No license is granted to third-party code, game assets, trademarks, or other intellectual property referenced by this project. Do not upload or redistribute content you do not have permission to share. See `Docs/` for project-specific notes.
