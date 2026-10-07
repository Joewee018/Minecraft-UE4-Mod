package crb.client;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.client.player.LocalPlayer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Visual-debug support: switches the real Minecraft window to the requested camera, waits for it to render, saves a
 * vanilla screenshot (the 1:1 reference) and a JSON dump of the pose surfaces exported at that moment.
 * Files go to Work\mc\screenshots and Work\debug (this project only).
 */
public final class DebugCapture {
    private static final Logger LOG = LoggerFactory.getLogger("crossover_rebuilt");
    private record Pending(String name, CameraType camera, int ticks) {}
    private final List<Pending> pending = new ArrayList<>();
    public static volatile JsonArray lastSurfaces = new JsonArray();

    public void request(Minecraft mc, String name, String camera) {
        if (pending.size() > 8 || !name.matches("[A-Za-z0-9_\\-]{1,64}")) return;
        CameraType type = switch (camera) { case "back" -> CameraType.THIRD_PERSON_BACK; case "front" -> CameraType.THIRD_PERSON_FRONT; default -> CameraType.FIRST_PERSON; };
        mc.options.setCameraType(type);
        pending.add(new Pending(name, type, 6));
    }

    /** Called every client tick (render thread). */
    public void tick(Minecraft mc) {
        for (int i = 0; i < pending.size(); i++) {
            Pending p = pending.get(i);
            if (p.ticks() > 0) { pending.set(i, new Pending(p.name(), p.camera(), p.ticks() - 1)); continue; }
            pending.remove(i--);
            try {
                Screenshot.grab(mc.gameDirectory, "mc_" + p.name() + ".png", mc.getMainRenderTarget(), msg -> { });
                LocalPlayer pl = mc.player;
                JsonObject d = new JsonObject();
                d.addProperty("name", p.name());
                d.addProperty("camera", p.camera().name());
                if (pl != null) {
                    d.addProperty("yRot", pl.getYRot()); d.addProperty("xRot", pl.getXRot());
                    d.addProperty("yBodyRot", pl.yBodyRot); d.addProperty("yHeadRot", pl.yHeadRot);
                    d.addProperty("mainhand", pl.getMainHandItem().toString()); d.addProperty("offhand", pl.getOffhandItem().toString());
                    d.addProperty("x", pl.getX()); d.addProperty("y", pl.getY()); d.addProperty("z", pl.getZ());
                }
                d.addProperty("window", mc.getWindow().getWidth() + "x" + mc.getWindow().getHeight());
                d.addProperty("fov", mc.options.fov().get());
                d.add("surfaces", lastSurfaces);
                Path dir = mc.gameDirectory.toPath().getParent().resolve("debug");
                Files.createDirectories(dir);
                Files.writeString(dir.resolve(p.name() + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(d), StandardCharsets.UTF_8);
            } catch (Exception ex) {
                LOG.warn("debug capture {} failed: {}", p.name(), ex.toString());
            }
            mc.options.setCameraType(CameraType.FIRST_PERSON);
        }
    }
}
