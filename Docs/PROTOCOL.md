# Crossover-Rebuilt bridge protocol 1

Frame (little endian): `u32 magic "CRB1" | u16 type | u16 flags | u32 seq | u32 jsonLen | u32 binLen | json | bin`.
Limits: json <= 256 KiB, bin <= 1 MiB; anything else is rejected before allocation (Java `Wire.read`, UE `Crb::ParseHeader`).
Discovery: Java binds 127.0.0.1:0 and atomically writes `Work\bridge-endpoint.json` {protocol, port, token(64 hex), session}.
The host must send HELLO with the token within 5 s; one controlling host, a new authenticated host replaces the old one.

| Type | Dir | Content |
|---|---|---|
| 1 HELLO / 2 WELCOME / 3 BYE | both | token+protocol / versions + loaded Fabric mods / close |
| 10 INPUT | UE->J | fwd, strafe, jump, sneak, sprint, attack, use, press counters, slot, yaw, pitch, lease (<=500 ms) |
| 11 COMMAND / 12 RESULT | UE->J / J->UE | `{id, op, args}` -> `{id, op, ok, message, thread, ...}` |
| 20 STATE | J->UE (latest) | position, look, vitals, armor, food, XP, 41 slots, hit, gravity view, fixture, counters |
| 21 SECTION | J->UE (FIFO) | `{sx,sy,sz,rev,empty,palette[]}` + u16[4096] palette indices + u8[4096] (sky<<4|block) |
| 22 WINDOW | J->UE | streamed window centre and radius; UE drops sections outside |
| 23 MODEL | J->UE (before first use) | baked quads: 4x(x,y,z,u,v f32) + tint ARGB + cull i8 + face i8 + shade u8 + pad (88 B), <=512 |
| 24 TEXTURE | J->UE (own lane) | `{name, gen, w, h, total, offset, last}` + PNG chunk (<=192 KB); total <=16 MB, <=4096 px |
| 25 POSE | J->UE (latest) | `{seq, tick, x,y,z, walkPos, walkSpeed, attack, using, mainhand, groups[{g,tex,layer,count}]}` + 28 B vertices (x,y,z,u,v f32, ARGB, normal i8x3, pad), <=24576 |
| 26 LIGHTMAP | J->UE (latest) | 16x16 BGRA of Minecraft's LightTexture (x = block light, y = sky light) |
| 27 PARTICLES | J->UE (latest) | `{cx,cy,cz,count}` + 48 B per particle (id, centre, size, uv rect, ARGB, sheet, block, sky), <=768 |
| 30 EVENT | J->UE | e.g. orbital `charging` / `impact {removed}` |

Commands: `world.info, player.reset, fixture.list, fixture.toggle{id}, fixture.off, time.set{time}, gravity.grab,
gravity.distance{delta|value}, gravity.release, orbital.fire, ping, mods, resend`. Fixture/teleport/time/orbital ops are
refused unless `-Dcrb.devWorld=true`, the level name starts with `crb-superflat` and the generator is `FlatLevelSource`.

Coordinates: UE cm = (100(z-az), -100(x-ax), 100(y-ay)) around a section-aligned double anchor; MC yaw = UE yaw,
MC pitch = -UE pitch. Hand vertices are OpenGL view space -> UE camera space (-z, x, y) x 100.
