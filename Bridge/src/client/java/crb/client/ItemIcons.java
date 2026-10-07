package crb.client;

import com.mojang.blaze3d.pipeline.TextureTarget;
import com.mojang.blaze3d.platform.Lighting;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexSorting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.world.item.ItemStack;
import org.joml.Matrix4f;

import java.util.List;

/**
 * Renders item stacks with Minecraft's own GUI item renderer into an offscreen target and reads the pixels back,
 * producing the exact hotbar/inventory icons (3D block items, tints, durability-independent). Render thread only.
 */
public final class ItemIcons {
    public static final int CELL = 32, COLS = 16, ROWS = 4, WIDTH = CELL * COLS, HEIGHT = CELL * ROWS;
    private static TextureTarget target;

    public static byte[] render(Minecraft mc, List<ItemStack> stacks) throws Exception {
        RenderSystem.assertOnRenderThread();
        if (target == null) target = new TextureTarget(WIDTH, HEIGHT, true, Minecraft.ON_OSX);
        Matrix4f oldProjection = new Matrix4f(RenderSystem.getProjectionMatrix());
        VertexSorting oldSorting = RenderSystem.getVertexSorting();
        PoseStack mv = RenderSystem.getModelViewStack();
        mv.pushPose();
        try {
            target.setClearColor(0, 0, 0, 0);
            target.clear(Minecraft.ON_OSX);
            target.bindWrite(true);
            RenderSystem.setProjectionMatrix(new Matrix4f().setOrtho(0, WIDTH / 2f, HEIGHT / 2f, 0, 1000f, 21000f), VertexSorting.ORTHOGRAPHIC_Z);
            mv.setIdentity();
            mv.translate(0, 0, -11000f);
            RenderSystem.applyModelViewMatrix();
            Lighting.setupFor3DItems();
            GuiGraphics g = new GuiGraphics(mc, mc.renderBuffers().bufferSource());
            for (int i = 0; i < stacks.size() && i < COLS * ROWS; i++) {
                ItemStack s = stacks.get(i);
                if (s == null || s.isEmpty()) continue;
                g.renderItem(s, (i % COLS) * (CELL / 2), (i / COLS) * (CELL / 2));
            }
            g.flush();
            try (NativeImage img = new NativeImage(WIDTH, HEIGHT, false)) {
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
