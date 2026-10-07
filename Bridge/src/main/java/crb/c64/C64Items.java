package crb.c64;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.item.Item;

import java.util.EnumMap;
import java.util.Map;

/** Craft 64 weapon pickups: one Minecraft item per weapon (16x16 Minecraft-style textures in assets/crossover_rebuilt). */
public final class C64Items {
    private C64Items() { }
    private static final Map<C64Weapon, Item> ITEMS = new EnumMap<>(C64Weapon.class);

    public static void register() {
        for (C64Weapon w : C64Weapon.values())
            ITEMS.put(w, Registry.register(BuiltInRegistries.ITEM, new ResourceLocation("crossover_rebuilt", "c64_" + w.key), new Item(new Item.Properties().stacksTo(1))));
    }

    public static Item weapon(String key) { C64Weapon w = C64Weapon.byKey(key); return w == null ? null : ITEMS.get(w); }
}
