# Toolchain and runtime versions (recorded on this machine, 2026-10-06)

| Component | Version | Evidence |
|---|---|---|
| Unreal Engine | 4.27.2 (CL 18319896, ++UE4+Release-4.27) | `Engine\Build\Build.version` (Logs\probe.txt) |
| Compiler | Visual Studio 2019 Build Tools 16.11.60, MSVC 14.29.30133, Windows SDK 10.0.19041.0 | vswhere + UBT log |
| Java | Microsoft OpenJDK 17.0.20.1 (`C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot`) | `java -version` |
| Minecraft | Java Edition 1.20.1 (real client + integrated server, Fabric dev launch) | WELCOME frame / LiveProbe |
| Fabric Loader | 0.17.2 | WELCOME frame |
| Fabric API | 0.92.6+1.20.1 | WELCOME frame |
| Fabric Loom / Gradle | 1.10.5 / 8.13 (wrapper, pinned SHA-256) | build log |
| Mappings | Mojang official (`loom.officialMojangMappings()`), same as the reference workspace | build.gradle |
| Bridge | crossover-rebuilt-bridge 1.0.0, protocol 1 (`CRB1` frames) | Logs\bridge-build.txt (jar SHA-256) |
| From The Fog (Herobrine, dev client only) | 1.9.2 for 1.20-1.20.2 Forge/Fabric, mod id `watching`, Lunar Eclipse Studios, CC BY-NC-SA 4.0; `From-The-Fog-1.20-v1.9.2-Forge-Fabric.jar`, SHA-512 `9183862...c8e341d` | Tools\InstallMods.ps1, Work\mc\mods |

Reference workspace check: `Minecraft-1.20.1-Workspace\gradle.properties` = `minecraft_version=1.20.1`, mappings
`loom.officialMojangMappings()`. Decompiled sources (Loom genSources, this project's own cache) were used only to read
API signatures and behaviour; nothing decompiled is copied into the bridge or distributed.
