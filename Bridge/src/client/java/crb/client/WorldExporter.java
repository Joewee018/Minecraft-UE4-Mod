package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import crb.Bin;
import crb.Endpoint;
import crb.Wire;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.ItemBlockRenderTypes;
import net.minecraft.client.renderer.RenderType;
import net.minecraft.client.renderer.block.model.BakedQuad;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.resources.model.BakedModel;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.SectionPos;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.ChunkStatus;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.material.FluidState;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Streams a bounded window of copied chunk sections (block-state ids + per-cell sky/block light) and the baked
 * model quads of each state id the host has not seen. Reads the client level on the client thread only.
 */
public final class WorldExporter {
    // Radius 6 = the client renderDistance written by StartMinecraft (vanilla fog ends there too).
    public static final int RADIUS = 6, RADIUS_Y = 2, SCANS_PER_TICK = 12, NEAR = 27, MAX_QUADS = 512;
    private final Map<Long, Long> sentHash = new HashMap<>();
    private final Map<Long, Integer> revisions = new HashMap<>();
    private final Set<Integer> sentModels = new HashSet<>();
    private final List<long[]> order = new ArrayList<>();
    private int cursor, nearCursor;
    private int cx = Integer.MIN_VALUE, cy, cz;
    public long sections, models, modelErrors;
    public String lastError = "";

    public void reset() { sentHash.clear(); revisions.clear(); sentModels.clear(); order.clear(); cursor = 0; cx = Integer.MIN_VALUE; }

    /** Forget a section so it is re-sent (e.g. after a fixture change) — cheap, everything else is hashed. */
    public void invalidateModels() { sentModels.clear(); sentHash.clear(); }

    public void tick(Minecraft mc, Endpoint endpoint) {
        ClientLevel level = mc.level;
        if (level == null || mc.player == null) return;
        SectionPos sp = SectionPos.of(mc.player.blockPosition());
        if (sp.x() != cx || sp.y() != cy || sp.z() != cz) {
            cx = sp.x(); cy = sp.y(); cz = sp.z();
            JsonObject w = new JsonObject();
            w.addProperty("sx", cx); w.addProperty("sy", cy); w.addProperty("sz", cz); w.addProperty("r", RADIUS); w.addProperty("ry", RADIUS_Y);
            if (!endpoint.world(Wire.WINDOW, w.toString(), null)) { cx = Integer.MIN_VALUE; return; }
            order.clear();
            for (int dy = -RADIUS_Y; dy <= RADIUS_Y; dy++)
                for (int dx = -RADIUS; dx <= RADIUS; dx++)
                    for (int dz = -RADIUS; dz <= RADIUS; dz++) order.add(new long[] { cx + dx, cy + dy, cz + dz, Math.abs(dx) + Math.abs(dy) + Math.abs(dz) });
            order.sort((a, b) -> Long.compare(a[3], b[3]));
            sentHash.keySet().removeIf(k -> {
                int x = SectionPos.x(k), y = SectionPos.y(k), z = SectionPos.z(k);
                return Math.abs(x - cx) > RADIUS || Math.abs(z - cz) > RADIUS || Math.abs(y - cy) > RADIUS_Y;
            });
            cursor = 0; nearCursor = 0;
        }
        if (order.isEmpty()) return;
        // Half the budget keeps the sections around the player fresh (block edits show within ~0.25 s); the other half
        // sweeps the whole window round-robin (unchanged sections hash-match and are not resent).
        int near = Math.min(NEAR, order.size());
        for (int n = 0; n < SCANS_PER_TICK && endpoint.worldFree() > 24; n++) {
            long[] s;
            if ((n & 1) == 0) { s = order.get(nearCursor); nearCursor = (nearCursor + 1) % near; }
            else { s = order.get(cursor); cursor = (cursor + 1) % order.size(); }
            scan(mc, level, endpoint, (int) s[0], (int) s[1], (int) s[2]);
        }
    }

    private void scan(Minecraft mc, ClientLevel level, Endpoint endpoint, int sx, int sy, int sz) {
        if (sy < level.getMinSection() || sy >= level.getMaxSection()) return;
        LevelChunk chunk = level.getChunkSource().getChunk(sx, sz, ChunkStatus.FULL, false);
        if (chunk == null) return;
        LevelChunkSection section = chunk.getSections()[level.getSectionIndexFromSectionY(sy)];
        boolean empty = section == null || section.hasOnlyAir();
        BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
        byte[] light = new byte[4096];
        short[] idx = empty ? null : new short[4096];
        List<Integer> palette = new ArrayList<>();
        Map<Integer, Integer> paletteIndex = new HashMap<>();
        long hash = empty ? 1 : 7;
        int bx = sx << 4, by = sy << 4, bz = sz << 4;
        for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            int c = (y * 16 + z) * 16 + x;
            pos.set(bx + x, by + y, bz + z);
            int sky = level.getBrightness(LightLayer.SKY, pos), blk = level.getBrightness(LightLayer.BLOCK, pos);
            light[c] = (byte) ((sky << 4) | blk);
            hash = hash * 31 + light[c];
            if (!empty) {
                int id = Block.getId(section.getBlockState(x, y, z));
                Integer pi = paletteIndex.get(id);
                if (pi == null) { pi = palette.size(); palette.add(id); paletteIndex.put(id, pi); }
                idx[c] = (short) (int) pi;
                hash = hash * 1000003L + id;
            }
        }
        long key = SectionPos.asLong(sx, sy, sz);
        Long prev = sentHash.get(key);
        if (prev != null && prev == hash) return;
        // Models first (same FIFO lane), so the host can mesh as soon as the section arrives.
        if (!empty) for (int id : palette) if (!sentModels.contains(id)) { if (!sendModel(mc, level, endpoint, id)) return; }
        int rev = revisions.merge(key, 1, Integer::sum);
        JsonObject j = new JsonObject();
        j.addProperty("sx", sx); j.addProperty("sy", sy); j.addProperty("sz", sz); j.addProperty("rev", rev); j.addProperty("empty", empty);
        Bin bin = new Bin(empty ? 4096 : 12288, 12288);
        if (!empty) {
            JsonArray pa = new JsonArray();
            for (int id : palette) pa.add(id);
            j.add("palette", pa);
            for (short v : idx) bin.u16(v);
        }
        bin.bytes(light, 0, 4096);
        if (endpoint.world(Wire.SECTION, j.toString(), bin.toArray())) { sentHash.put(key, hash); sections++; }
    }

    /** Sends the model for a block-state id. Returns false if the lane is full (retry next scan). */
    public boolean sendModel(Minecraft mc, ClientLevel level, Endpoint endpoint, int id) {
        BlockState state = Block.stateById(id);
        JsonObject j = new JsonObject();
        j.addProperty("id", id);
        j.addProperty("state", state.toString());
        Bin bin = new Bin(4096, MAX_QUADS * 88);
        int quads = 0;
        try {
            RenderType rt = ItemBlockRenderTypes.getChunkRenderType(state);
            int layer = rt == RenderType.translucent() ? 2 : (rt == RenderType.solid() ? 0 : 1);
            FluidState fluid = state.getFluidState();
            if (!fluid.isEmpty() && state.getRenderShape() == net.minecraft.world.level.block.RenderShape.INVISIBLE) {
                layer = 2;
                quads = fluidQuads(mc, level, state, fluid, bin);
            } else if (!state.isAir()) {
                BakedModel model = mc.getBlockRenderer().getBlockModel(state);
                BlockPos at = mc.player.blockPosition();
                for (Direction dir : DIRS) {
                    for (BakedQuad q : model.getQuads(state, dir, RandomSource.create(42))) {
                        if (quads >= MAX_QUADS) break;
                        writeQuad(mc, level, state, q, dir, at, bin); quads++;
                    }
                }
            }
            j.addProperty("layer", layer);
            j.addProperty("opaque", state.isSolidRender(EmptyBlockGetter.INSTANCE, BlockPos.ZERO));
            j.addProperty("emit", state.getLightEmission());
            j.addProperty("air", state.isAir());
            j.addProperty("quads", quads);
        } catch (Exception ex) {
            modelErrors++; lastError = state + ": " + ex;
            j.addProperty("layer", 0); j.addProperty("opaque", false); j.addProperty("emit", 0); j.addProperty("air", false); j.addProperty("quads", 0);
            bin = new Bin(0, 0);
        }
        if (!endpoint.world(Wire.MODEL, j.toString(), bin.toArray())) return false;
        sentModels.add(id);
        models++;
        return true;
    }

    private static final Direction[] DIRS = { null, Direction.DOWN, Direction.UP, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST };

    private static void writeQuad(Minecraft mc, ClientLevel level, BlockState state, BakedQuad q, Direction cullDir, BlockPos at, Bin bin) {
        int[] v = q.getVertices();
        int stride = v.length / 4;
        for (int i = 0; i < 4; i++) {
            int o = i * stride;
            bin.f32(Float.intBitsToFloat(v[o])).f32(Float.intBitsToFloat(v[o + 1])).f32(Float.intBitsToFloat(v[o + 2]));
            bin.f32(Float.intBitsToFloat(v[o + 4])).f32(Float.intBitsToFloat(v[o + 5]));
        }
        int tint = 0xFFFFFFFF;
        if (q.isTinted()) tint = 0xFF000000 | mc.getBlockColors().getColor(state, level, at, q.getTintIndex());
        bin.u32(tint);
        // Cull face: vanilla culls a quad against its declared direction when it lies on the block boundary.
        // Quads returned for a direction are culled against that neighbour (vanilla semantics); null-direction quads never are.
        Direction d = q.getDirection();
        bin.u8(cullDir != null ? cullDir.get3DDataValue() : -1);
        bin.u8(d.get3DDataValue());
        bin.u8(q.isShade() ? 1 : 0);
        bin.u8(0);
    }

    private static boolean onBoundary(int[] v, int stride, Direction d) {
        for (int i = 0; i < 4; i++) {
            float c = switch (d.getAxis()) {
                case X -> Float.intBitsToFloat(v[i * stride]);
                case Y -> Float.intBitsToFloat(v[i * stride + 1]);
                case Z -> Float.intBitsToFloat(v[i * stride + 2]);
            };
            float target = d.getAxisDirection() == Direction.AxisDirection.POSITIVE ? 1f : 0f;
            if (Math.abs(c - target) > 1.0e-4f) return false;
        }
        return true;
    }

    private static int fluidQuads(Minecraft mc, ClientLevel level, BlockState state, FluidState fluid, Bin bin) {
        TextureAtlasSprite sprite = mc.getModelManager().getBlockModelShaper().getParticleIcon(state);
        int tint = 0xFF000000 | mc.getBlockColors().getColor(state, level, mc.player.blockPosition(), 0);
        float h = Math.max(0.1f, Math.min(1f, fluid.getOwnHeight()));
        float u0 = sprite.getU0(), u1 = sprite.getU1(), v0 = sprite.getV0(), v1 = sprite.getV1();
        float vh = v0 + (v1 - v0) * (1 - h);
        // Up, down, north, south, west, east (positions in block units; CCW as seen from outside).
        float[][] faces = {
            { 0, h, 0, 0, h, 1, 1, h, 1, 1, h, 0 },
            { 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1 },
            { 1, h, 0, 1, 0, 0, 0, 0, 0, 0, h, 0 },
            { 0, h, 1, 0, 0, 1, 1, 0, 1, 1, h, 1 },
            { 0, h, 0, 0, 0, 0, 0, 0, 1, 0, h, 1 },
            { 1, h, 1, 1, 0, 1, 1, 0, 0, 1, h, 0 } };
        Direction[] dirs = { Direction.UP, Direction.DOWN, Direction.NORTH, Direction.SOUTH, Direction.WEST, Direction.EAST };
        for (int f = 0; f < 6; f++) {
            float[] p = faces[f];
            boolean top = dirs[f] == Direction.UP;
            float[][] uv = top || dirs[f] == Direction.DOWN
                ? new float[][] { { u0, v0 }, { u0, v1 }, { u1, v1 }, { u1, v0 } }
                : new float[][] { { u0, vh }, { u0, v1 }, { u1, v1 }, { u1, vh } };
            for (int i = 0; i < 4; i++) bin.f32(p[i * 3]).f32(p[i * 3 + 1]).f32(p[i * 3 + 2]).f32(uv[i][0]).f32(uv[i][1]);
            bin.u32(tint);
            bin.u8(top && h < 1f ? -1 : dirs[f].get3DDataValue());
            bin.u8(dirs[f].get3DDataValue());
            bin.u8(1); bin.u8(0);
        }
        return 6;
    }
}
