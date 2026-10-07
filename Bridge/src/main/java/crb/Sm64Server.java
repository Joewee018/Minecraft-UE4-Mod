package crb;

import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;

import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Server half of SM64 Steve Movement: players the client-side controller is driving take no vanilla fall damage
 * (the SM64 jumps fall much further than vanilla ones). Set by the client through server.execute; cleared on logout.
 */
public final class Sm64Server {
    private Sm64Server() { }
    private static final Set<UUID> NO_FALL = ConcurrentHashMap.newKeySet();

    public static void set(UUID id, boolean on) { if (on) NO_FALL.add(id); else NO_FALL.remove(id); }
    public static boolean noFall(UUID id) { return NO_FALL.contains(id); }
    public static int count() { return NO_FALL.size(); }

    /** Each server tick (start and end): keep the fall distance at zero as well, so no landing path can damage. */
    public static void tick(MinecraftServer server) {
        if (NO_FALL.isEmpty()) return;
        for (UUID id : NO_FALL) {
            ServerPlayer p = server.getPlayerList().getPlayer(id);
            if (p != null) p.resetFallDistance();
        }
    }

    public static void clear() { NO_FALL.clear(); }
}
