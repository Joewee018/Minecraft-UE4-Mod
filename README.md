# Crossover-Rebuilt

An unofficial hybrid modding project that pairs a Java/Fabric Bridge with an Unreal Engine 4.27 presentation layer.

## Repository contents

This public repository contains project source code, project configuration, build scripts, and documentation. It does **not** contain a packaged game build, Minecraft or other third-party game assets, Unreal content assets, map archives, or generated build output. Those files remain outside this repository and must be obtained or used only with the rights required by their respective owners.

This repository is not a standalone game download. It does not include Mojang/Microsoft code or assets, or other proprietary game content.

## Architecture

- `Bridge/` contains the Fabric mod source. Java owns world state, physics, combat, and AI.
- `Unreal/Source/` contains the UE4 C++ host. Unreal handles rendering, input, HUD, menus, characters, and camera presentation.
- `Unreal/Scripts/` contains editor-side asset/build scripts. Generated and content asset files are excluded.
- `Tools/` contains local build and test scripts.

## Local development

The project targets Unreal Engine 4.27 and Minecraft 1.20.1 with Fabric. Build and test prerequisites are not bundled here. Use the project documentation and scripts after setting up the required tools and locally sourced assets. Automated game tests should use disposable superflat worlds.

## Rights and use

No license is granted to third-party code, game assets, trademarks, or other intellectual property referenced by this project. Do not upload or redistribute content you do not have permission to share. See `Docs/` for project-specific notes.
