package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.NativeImage;
import crb.Endpoint;
import crb.ServerOps;
import crb.Wire;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;

import java.util.ArrayList;
import java.util.List;

/** Exports Java-owned player state (vitals, XP, inventory, target, time) every tick plus the light map and icons. */
public final class StateExporter {
    public static final ResourceLocation ICONS = new ResourceLocation("textures/gui/icons.png");
    public static final ResourceLocation WIDGETS = new ResourceLocation("textures/gui/widgets.png");
    public static final ResourceLocation FONT = new ResourceLocation("textures/font/ascii.png");
    public static final ResourceLocation INVENTORY = new ResourceLocation("textures/gui/container/inventory.png");
    public static final String ICON_SHEET = "crb:item_icons";
    static final String[] CREATIVE = { "textures/gui/container/creative_inventory/tabs.png", "textures/gui/container/creative_inventory/tab_items.png",
        "textures/gui/container/creative_inventory/tab_item_search.png", "textures/gui/container/creative_inventory/tab_inventory.png" };
    private String iconSignature = "";
    private long lastIconNanos;
    private final ChalkWallBuys chalk = new ChalkWallBuys();
    private int iconGeneration;
    private final int[] lastLight = new int[256];
    public long states, iconSheets, iconErrors, lightmaps;
    public String lastError = "";

    public void reset() { iconSignature = ""; java.util.Arrays.fill(lastLight, 0); }

    public void tick(Minecraft mc, Endpoint endpoint, TextureExporter textures, JsonObject extra) {
        LocalPlayer p = mc.player;
        if (p == null || mc.level == null) return;
        textures.request(ICONS);
        textures.request(WIDGETS);
        textures.request(INVENTORY);
        textures.request(FONT);
        textures.request(new ResourceLocation("textures/gui/options_background.png")); // Unreal menus: list background
        // Creative inventory sheets (only needed while in creative; small, requested once).
        if (mc.gameMode != null && mc.gameMode.getPlayerMode().isCreative())
            for (String t : CREATIVE) textures.request(new ResourceLocation(t));
        Inventory inv = p.getInventory();
        List<ItemStack> iconStacks = new ArrayList<>();
        for (int i = 0; i < 9; i++) iconStacks.add(inv.items.get(i));
        iconStacks.add(inv.offhand.get(0));
        for (int i = 0; i < 4; i++) iconStacks.add(inv.armor.get(i));
        for (int i = 9; i < 36; i++) iconStacks.add(inv.items.get(i));
        // 41 carried (cursor) stack, 42-45 2x2 crafting grid, 46 crafting result (vanilla InventoryMenu slots 1-4, 0).
        iconStacks.add(p.inventoryMenu.getCarried());
        for (int i = 1; i <= 4; i++) iconStacks.add(p.inventoryMenu.getSlot(i).getItem());
        iconStacks.add(p.inventoryMenu.getSlot(0).getItem());

        JsonObject j = new JsonObject();
        j.addProperty("tick", mc.level.getGameTime());
        j.addProperty("dim", mc.level.dimension().location().toString());
        j.addProperty("x", p.getX()); j.addProperty("y", p.getY()); j.addProperty("z", p.getZ());
        j.addProperty("vx", p.getDeltaMovement().x); j.addProperty("vy", p.getDeltaMovement().y); j.addProperty("vz", p.getDeltaMovement().z);
        j.addProperty("yaw", p.getYRot()); j.addProperty("pitch", p.getXRot());
        j.addProperty("eye", p.getEyeHeight());
        j.addProperty("onGround", p.onGround()); j.addProperty("sneak", p.isShiftKeyDown()); j.addProperty("sprint", p.isSprinting());
        j.addProperty("inWater", p.isInWater());
        j.addProperty("flying", p.getAbilities().flying); j.addProperty("hurtTime", p.hurtTime);
        j.addProperty("health", p.getHealth()); j.addProperty("maxHealth", p.getMaxHealth()); j.addProperty("absorb", p.getAbsorptionAmount());
        j.addProperty("armor", p.getArmorValue());
        j.addProperty("food", p.getFoodData().getFoodLevel()); j.addProperty("sat", p.getFoodData().getSaturationLevel());
        j.addProperty("xpLevel", p.experienceLevel); j.addProperty("xpProgress", p.experienceProgress);
        j.addProperty("selected", inv.selected);
        j.addProperty("gamemode", mc.gameMode == null ? "" : mc.gameMode.getPlayerMode().getName());
        j.addProperty("dead", p.isDeadOrDying() || mc.screen instanceof net.minecraft.client.gui.screens.DeathScreen);
        j.addProperty("screen", mc.screen == null ? "" : mc.screen.getClass().getSimpleName());
        j.addProperty("dayTime", mc.level.getDayTime());
        j.addProperty("skyDarken", mc.level.getSkyDarken());
        j.addProperty("sunAngle", mc.level.getSunAngle(1f));
        j.addProperty("rain", mc.level.getRainLevel(1f));
        // Vanilla sky (zenith) and fog (horizon) colours, so Unreal's sky matches Minecraft's time/biome colours.
        net.minecraft.world.phys.Vec3 sky = mc.level.getSkyColor(mc.gameRenderer.getMainCamera().getPosition(), 1f);
        com.google.gson.JsonArray skyA = new com.google.gson.JsonArray(); skyA.add(sky.x); skyA.add(sky.y); skyA.add(sky.z);
        j.add("sky", skyA);
        com.google.gson.JsonArray fogA = new com.google.gson.JsonArray();
        fogA.add(net.minecraft.client.renderer.FogRenderer.fogRed); fogA.add(net.minecraft.client.renderer.FogRenderer.fogGreen); fogA.add(net.minecraft.client.renderer.FogRenderer.fogBlue);
        j.add("fog", fogA);
        BlockPos eye = BlockPos.containing(p.getEyePosition());
        j.addProperty("eyeSky", mc.level.getBrightness(LightLayer.SKY, eye));
        j.addProperty("eyeBlock", mc.level.getBrightness(LightLayer.BLOCK, eye));
        JsonArray hotbar = new JsonArray();
        for (int i = 0; i < iconStacks.size(); i++) hotbar.add(stack(iconStacks.get(i), i));
        j.add("slots", hotbar); // 0-8 hotbar, 9 offhand, 10-13 armor (feet..head), 14-40 main, 41 cursor, 42-45 craft grid, 46 result
        HitResult hit = mc.hitResult;
        if (hit instanceof BlockHitResult bh && hit.getType() == HitResult.Type.BLOCK) {
            JsonObject h = new JsonObject();
            h.addProperty("x", bh.getBlockPos().getX()); h.addProperty("y", bh.getBlockPos().getY()); h.addProperty("z", bh.getBlockPos().getZ());
            h.addProperty("face", bh.getDirection().get3DDataValue());
            h.addProperty("state", Block.getId(mc.level.getBlockState(bh.getBlockPos())));
            // Exact vanilla outline: the edges of the block's outline VoxelShape (LevelRenderer.renderHitOutline).
            JsonArray edges = new JsonArray();
            mc.level.getBlockState(bh.getBlockPos()).getShape(mc.level, bh.getBlockPos(), net.minecraft.world.phys.shapes.CollisionContext.of(p))
                .forAllEdges((x1, y1, z1, x2, y2, z2) -> { if (edges.size() < 6 * 96) { edges.add(x1); edges.add(y1); edges.add(z1); edges.add(x2); edges.add(y2); edges.add(z2); } });
            h.add("edges", edges);
            j.add("hit", h);
        }
        ServerOps.GravityView gv = ServerOps.gravityView;
        JsonObject g = new JsonObject();
        g.addProperty("holding", gv.holding()); g.addProperty("state", gv.stateId());
        g.addProperty("x", gv.x()); g.addProperty("y", gv.y()); g.addProperty("z", gv.z()); g.addProperty("distance", gv.distance());
        j.add("gravity", g);
        crb.AxeEntity.View av = crb.AxeEntity.VIEW;
        JsonObject ax = new JsonObject();
        ax.addProperty("active", av.active()); ax.addProperty("phase", av.phase());
        ax.addProperty("x", av.x()); ax.addProperty("y", av.y()); ax.addProperty("z", av.z());
        ax.addProperty("spin", av.spin()); ax.addProperty("yaw", av.yaw());
        ax.addProperty("travelled", av.travelled()); ax.addProperty("hits", av.hits()); ax.addProperty("tick", av.tick());
        j.add("axe", ax);
        // God of War Unity port: Mutant enemies near the player (drawn by Unreal with the Mutant skeletal mesh).
        JsonArray muts = new JsonArray();
        for (net.minecraft.world.entity.Entity e : mc.level.entitiesForRendering()) {
            if (!(e instanceof crb.MutantEntity m) || muts.size() >= 16 || m.distanceToSqr(p) > 64 * 64) continue;
            JsonObject o = new JsonObject();
            o.addProperty("id", m.getId());
            o.addProperty("x", m.getX()); o.addProperty("y", m.getY()); o.addProperty("z", m.getZ());
            o.addProperty("yaw", m.yBodyRot); o.addProperty("health", m.getHealth()); o.addProperty("maxHealth", m.getMaxHealth());
            o.addProperty("deathTime", m.deathTime); o.addProperty("hurtTime", m.hurtTime);
            muts.add(o);
        }
        j.add("mutants", muts);
        // Zombies mode: match state (server-authoritative, integrated server) + zombies near the player.
        crb.zm.ZombiesGame.View zv = crb.zm.ZombiesGame.VIEW;
        JsonObject zm = new JsonObject();
        zm.addProperty("phase", zv.phase()); zm.addProperty("round", zv.round()); zm.addProperty("points", zv.points());
        zm.addProperty("kills", zv.kills()); zm.addProperty("headshots", zv.headshots()); zm.addProperty("left", zv.zombiesLeft());
        JsonArray perks = new JsonArray(); for (String pk : zv.perks()) perks.add(pk); zm.add("perks", perks);
        zm.addProperty("prompt", zv.prompt()); zm.addProperty("cost", zv.promptCost()); zm.addProperty("afford", zv.promptAfford());
        zm.addProperty("instaKill", zv.instaKill()); zm.addProperty("doublePoints", zv.doublePoints());
        zm.addProperty("powerUp", zv.lastPowerUp()); zm.addProperty("powerUpTick", zv.lastPowerUpTick());
        zm.addProperty("crate", zv.crate()); zm.addProperty("roundTick", zv.roundTick());
        zm.addProperty("message", zv.message()); zm.addProperty("messageTick", zv.messageTick());
        zm.addProperty("tick", crb.zm.ZombiesGame.INSTANCE.tickCount());
        zm.addProperty("bulletsFired", zv.bulletsFired()); zm.addProperty("heldMag", zv.heldMag()); zm.addProperty("zombieHits", zv.zombieHits());
        zm.addProperty("capacity", zv.heldCapacity()); zm.addProperty("reserve", zv.reserve());
        java.util.Map<Integer, int[]> anims = new java.util.HashMap<>();
        for (int[] a : zv.zombieAnims()) anims.put(a[0], a);
        JsonArray zs = new JsonArray();
        if (!"OFF".equals(zv.phase()))
            for (net.minecraft.world.entity.Entity e : mc.level.entitiesForRendering()) {
                if (!(e instanceof crb.zm.ZmZombie z) || zs.size() >= 32 || z.distanceToSqr(p) > 64 * 64) continue;
                JsonObject o = new JsonObject();
                o.addProperty("id", z.getId());
                o.addProperty("x", z.getX()); o.addProperty("y", z.getY()); o.addProperty("z", z.getZ());
                o.addProperty("yaw", z.yBodyRot); o.addProperty("health", z.getHealth()); o.addProperty("maxHealth", z.getMaxHealth());
                o.addProperty("deathTime", z.deathTime); o.addProperty("hurtTime", z.hurtTime);
                int[] a = anims.get(z.getId());
                // AI state from the entity's synced data (also covers dying zombies, which the match list no longer holds).
                o.addProperty("anim", z.zState().ordinal()); o.addProperty("inside", a != null && a[2] == 1);
                zs.add(o);
            }
        zm.add("zombies", zs);
        zm.add("wallbuys", chalk.export(mc, textures));
        zm.addProperty("chalkSheet", ChalkWallBuys.SHEET);
        zm.addProperty("chalkGen", textures.generationOf(ChalkWallBuys.SHEET));
        zm.addProperty("box", crb.zm.ZombiesGame.INSTANCE.boxPhase());
        j.add("zm", zm);
        // Glass shatter events (projectile-broken glass) from the last two seconds; Unreal dedupes by seq.
        JsonArray sh = new JsonArray();
        long gt = mc.getSingleplayerServer() != null ? mc.getSingleplayerServer().overworld().getGameTime() : mc.level.getGameTime();
        for (crb.Shatter.Event e : crb.Shatter.recent(gt)) {
            JsonObject o = new JsonObject();
            o.addProperty("seq", e.seq()); o.addProperty("x", e.x()); o.addProperty("y", e.y()); o.addProperty("z", e.z());
            o.addProperty("state", e.state()); o.addProperty("dx", e.dx()); o.addProperty("dy", e.dy()); o.addProperty("dz", e.dz());
            o.addProperty("light", e.light()); o.addProperty("source", e.source()); o.addProperty("age", gt - e.tick());
            sh.add(o);
        }
        j.add("shatter", sh);
        JsonObject mp = new JsonObject();
        mp.addProperty("current", Maps.current); mp.addProperty("loading", Maps.loading); mp.addProperty("status", Maps.status); mp.addProperty("progress", Maps.progress);
        mp.addProperty("world", mc.getSingleplayerServer() != null ? mc.getSingleplayerServer().getWorldData().getLevelName() : "");
        j.add("maps", mp);
        j.addProperty("shatterTotal", crb.Shatter.total);
        j.addProperty("fixture", ServerOps.activeFixture);
        j.addProperty("iconSheet", ICON_SHEET);
        j.addProperty("iconGen", textures.generationOf(ICON_SHEET));
        j.addProperty("iconCell", ItemIcons.CELL); j.addProperty("iconCols", ItemIcons.COLS);
        for (String k : extra.keySet()) j.add(k, extra.get(k));
        endpoint.latest(Endpoint.LANE_STATE, Wire.STATE, j.toString(), null);
        states++;

        // Icon sheet: regenerate on stack change, at most 4 times per second.
        StringBuilder sig = new StringBuilder();
        for (ItemStack s : iconStacks) sig.append(s.isEmpty() ? "-" : BuiltInRegistries.ITEM.getKey(s.getItem()) + "#" + s.getCount() + "#" + (s.hasTag() ? s.getTag().hashCode() : 0)).append('|');
        long now = System.nanoTime();
        if (!sig.toString().equals(iconSignature) && now - lastIconNanos > 250_000_000L) {
            try {
                textures.publish(ICON_SHEET, ItemIcons.render(mc, iconStacks), false);
                iconSignature = sig.toString(); iconSheets++;
            } catch (Exception ex) {
                iconErrors++; lastError = "icons: " + ex;
            }
            lastIconNanos = now;
        }

        // Live light map (16x16): block light on X, sky light on Y. Sent when it changes.
        if (mc.level.getGameTime() % 2 == 0) {
            NativeImage lm = mc.gameRenderer.lightTexture().lightPixels;
            byte[] bgra = new byte[16 * 16 * 4];
            boolean changed = false;
            for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
                int abgr = lm.getPixelRGBA(x, y);
                int i = y * 16 + x;
                if (abgr != lastLight[i]) { changed = true; lastLight[i] = abgr; }
                bgra[i * 4] = (byte) ((abgr >> 16) & 255); bgra[i * 4 + 1] = (byte) ((abgr >> 8) & 255); bgra[i * 4 + 2] = (byte) (abgr & 255); bgra[i * 4 + 3] = (byte) 255;
            }
            if (changed) {
                JsonObject l = new JsonObject();
                l.addProperty("w", 16); l.addProperty("h", 16); l.addProperty("format", "bgra8"); l.addProperty("tick", mc.level.getGameTime());
                endpoint.latest(Endpoint.LANE_LIGHTMAP, Wire.LIGHTMAP, l.toString(), bgra);
                lightmaps++;
            }
        }
    }

    private static JsonObject stack(ItemStack s, int slot) {
        JsonObject o = new JsonObject();
        o.addProperty("slot", slot);
        if (s.isEmpty()) { o.addProperty("id", ""); return o; }
        o.addProperty("id", BuiltInRegistries.ITEM.getKey(s.getItem()).toString());
        o.addProperty("count", s.getCount());
        o.addProperty("damage", s.getDamageValue());
        o.addProperty("maxDamage", s.getMaxDamage());
        o.addProperty("name", s.getHoverName().getString());
        return o;
    }
}
