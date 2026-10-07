package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.mojang.blaze3d.pipeline.TextureTarget;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexSorting;
import net.minecraft.client.GuiMessage;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.screens.DeathScreen;
import net.minecraft.network.chat.Component;
import net.minecraft.util.Mth;
import org.joml.Matrix4f;

import java.util.List;

/**
 * Vanilla on-screen text that is not part of the 3D world: the title/subtitle (mods such as From The Fog use
 * custom-font titles for their jumpscares), the HUD chat history and the death screen message.
 * Titles are drawn with Minecraft's own Font exactly as Gui.render does (scale 4 / 2 around the screen centre)
 * into an offscreen target the size of the GUI at 2x and published as a texture; Unreal stretches it over the
 * viewport and applies the per-tick vanilla fade alpha. Render thread only.
 */
public final class TitleExporter {
    public static final String TITLE_SHEET = "crb:title";
    private static TextureTarget target;
    private String signature = "";
    public long rendered, errors;
    public String lastError = "";

    public void tick(Minecraft mc, TextureExporter textures, JsonObject state) {
        var gui = mc.gui;
        Component title = gui.title, subtitle = gui.subtitle;
        int time = gui.titleTime;
        // Vanilla fade (Gui.render, partial tick ~0).
        int alpha = 0;
        if (title != null && time > 0) {
            float t = time;
            alpha = 255;
            if (time > gui.titleFadeOutTime + gui.titleStayTime) alpha = (int) ((gui.titleFadeInTime + gui.titleStayTime + gui.titleFadeOutTime - t) * 255f / Math.max(1, gui.titleFadeInTime));
            if (time <= gui.titleFadeOutTime) alpha = (int) (t * 255f / Math.max(1, gui.titleFadeOutTime));
            alpha = Mth.clamp(alpha, 0, 255);
        }
        int gw = mc.getWindow().getGuiScaledWidth(), gh = mc.getWindow().getGuiScaledHeight();
        String sig = alpha > 8 ? Component.Serializer.toJson(title) + "|" + (subtitle == null ? "" : Component.Serializer.toJson(subtitle)) + "|" + gw + "x" + gh : "";
        if (!sig.isEmpty() && !sig.equals(signature)) {
            try {
                textures.publish(TITLE_SHEET, render(mc, title, subtitle, gw, gh), false);
                rendered++;
                signature = sig;
            } catch (Exception ex) { errors++; lastError = ex.toString(); }
        }
        if (sig.isEmpty()) signature = "";
        JsonObject t = new JsonObject();
        t.addProperty("alpha", alpha > 8 ? alpha / 255f : 0f);
        t.addProperty("gen", textures.generationOf(TITLE_SHEET));
        t.addProperty("text", title == null ? "" : title.getString());
        t.addProperty("sub", subtitle == null ? "" : subtitle.getString());
        state.add("title", t);

        // HUD chat: the newest lines, with their age in GUI ticks (vanilla fades unfocused chat after 200 ticks).
        JsonArray chat = new JsonArray();
        List<GuiMessage> all = mc.gui.getChat().allMessages;
        int now = mc.gui.getGuiTicks();
        for (int i = 0; i < all.size() && chat.size() < 10; i++) {
            GuiMessage m = all.get(i); // newest first
            int age = now - m.addedTime();
            if (age > 200) break;
            JsonObject o = new JsonObject();
            String s = m.content().getString();
            o.addProperty("text", s.length() > 200 ? s.substring(0, 200) : s);
            o.addProperty("age", age);
            chat.add(o);
        }
        state.add("chat", chat);

        if (mc.screen instanceof DeathScreen ds) {
            Component cause = ds.causeOfDeath;
            state.addProperty("deathMessage", cause == null ? "" : cause.getString());
        }
        if (mc.player != null) state.addProperty("score", mc.player.getScore());
    }

    private static byte[] render(Minecraft mc, Component title, Component subtitle, int gw, int gh) throws Exception {
        RenderSystem.assertOnRenderThread();
        int w = Math.max(64, Math.min(2048, gw * 2)), h = Math.max(64, Math.min(2048, gh * 2));
        if (target == null || target.width != w || target.height != h) {
            if (target != null) target.destroyBuffers();
            target = new TextureTarget(w, h, true, Minecraft.ON_OSX);
        }
        Matrix4f oldProjection = new Matrix4f(RenderSystem.getProjectionMatrix());
        VertexSorting oldSorting = RenderSystem.getVertexSorting();
        PoseStack mv = RenderSystem.getModelViewStack();
        mv.pushPose();
        try {
            target.setClearColor(0, 0, 0, 0);
            target.clear(Minecraft.ON_OSX);
            target.bindWrite(true);
            RenderSystem.setProjectionMatrix(new Matrix4f().setOrtho(0, gw, gh, 0, 1000f, 21000f), VertexSorting.ORTHOGRAPHIC_Z);
            mv.setIdentity();
            mv.translate(0, 0, -11000f);
            RenderSystem.applyModelViewMatrix();
            GuiGraphics g = new GuiGraphics(mc, mc.renderBuffers().bufferSource());
            Font font = mc.font;
            RenderSystem.enableBlend();
            g.pose().pushPose();
            g.pose().translate(gw / 2f, gh / 2f, 0);
            g.pose().pushPose();
            g.pose().scale(4f, 4f, 4f);
            int n = font.width(title);
            g.drawString(font, title, -n / 2, -10, 0xFFFFFFFF);
            g.pose().popPose();
            if (subtitle != null) {
                g.pose().pushPose();
                g.pose().scale(2f, 2f, 2f);
                int o = font.width(subtitle);
                g.drawString(font, subtitle, -o / 2, 5, 0xFFFFFFFF);
                g.pose().popPose();
            }
            g.pose().popPose();
            g.flush();
            try (NativeImage img = new NativeImage(w, h, false)) {
                RenderSystem.bindTexture(target.getColorTextureId());
                img.downloadTexture(0, false);
                img.flipY();
                return img.asByteArray();
            }
        } finally {
            mv.popPose();
            RenderSystem.applyModelViewMatrix();
            RenderSystem.setProjectionMatrix(oldProjection, oldSorting);
            RenderSystem.bindTexture(0);
            mc.getMainRenderTarget().bindWrite(true);
        }
    }
}
