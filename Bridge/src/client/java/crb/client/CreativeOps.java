package crb.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.inventory.InventoryMenu;
import net.minecraft.world.item.CreativeModeTab;
import net.minecraft.world.item.CreativeModeTabs;
import net.minecraft.world.item.ItemStack;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Creative inventory driven from Unreal through vanilla client code: tab contents come from CreativeModeTabs
 * (built with the same feature flags / permissions as CreativeModeInventoryScreen), the carried stack lives in the
 * player's InventoryMenu like vanilla, and every slot change is sent with MultiPlayerGameMode.handleCreativeModeItemAdd
 * (ServerboundSetCreativeModeSlotPacket), which the server only accepts in creative mode. Render thread only.
 */
public final class CreativeOps {
    public static final String TABS_SHEET = "crb:creative_tabs", PAGE_SHEET = "crb:creative_page";
    public static final int COLS = 9, ROWS = 5;

    private static List<CreativeModeTab> tabs(Minecraft mc) {
        LocalPlayer p = mc.player;
        boolean ops = mc.options.operatorItemsTab().get() && p.canUseGameMasterBlocks();
        CreativeModeTabs.tryRebuildTabContents(p.connection.enabledFeatures(), ops, p.level().registryAccess());
        return CreativeModeTabs.tabs();
    }

    public static JsonObject tabsResult(Minecraft mc, TextureExporter textures) throws Exception {
        List<CreativeModeTab> list = tabs(mc);
        JsonArray a = new JsonArray();
        List<ItemStack> icons = new ArrayList<>();
        for (int i = 0; i < list.size(); i++) {
            CreativeModeTab t = list.get(i);
            JsonObject o = new JsonObject();
            o.addProperty("i", i);
            var key = BuiltInRegistries.CREATIVE_MODE_TAB.getKey(t);
            o.addProperty("id", key == null ? "" : key.toString());
            o.addProperty("name", t.getDisplayName().getString());
            o.addProperty("row", t.row() == CreativeModeTab.Row.TOP ? 0 : 1);
            o.addProperty("col", t.column());
            o.addProperty("right", t.isAlignedRight());
            o.addProperty("type", t.getType().name().toLowerCase(Locale.ROOT));
            o.addProperty("bg", t.getBackgroundSuffix());
            o.addProperty("scroll", t.canScroll());
            o.addProperty("showTitle", t.showTitle());
            a.add(o);
            icons.add(t.getIconItem());
        }
        textures.publish(TABS_SHEET, ItemIcons.render(mc, icons), false);
        JsonObject r = new JsonObject();
        r.add("tabs", a);
        r.addProperty("sheet", TABS_SHEET);
        return r;
    }

    private static List<ItemStack> items(Minecraft mc, int tab, String query) {
        List<CreativeModeTab> list = tabs(mc);
        if (tab < 0 || tab >= list.size()) return List.of();
        CreativeModeTab t = list.get(tab);
        List<ItemStack> out = new ArrayList<>();
        switch (t.getType()) {
            case CATEGORY -> out.addAll(t.getDisplayItems());
            case SEARCH -> {
                String q = query == null ? "" : query.trim().toLowerCase(Locale.ROOT);
                for (ItemStack s : t.getSearchTabDisplayItems()) {
                    if (q.isEmpty() || s.getHoverName().getString().toLowerCase(Locale.ROOT).contains(q)
                        || BuiltInRegistries.ITEM.getKey(s.getItem()).getPath().contains(q)) out.add(s);
                }
            }
            case HOTBAR -> {
                var hm = mc.getHotbarManager();
                for (int row = 0; row < 9; row++) for (ItemStack s : hm.get(row)) out.add(s.copy());
            }
            default -> { }
        }
        return out;
    }

    public static JsonObject page(Minecraft mc, TextureExporter textures, int tab, int row, String query) throws Exception {
        List<ItemStack> all = items(mc, tab, query);
        int rows = (all.size() + COLS - 1) / COLS;
        int maxRow = Math.max(0, rows - ROWS);
        row = Math.max(0, Math.min(row, maxRow));
        List<ItemStack> page = new ArrayList<>();
        JsonArray a = new JsonArray();
        for (int k = 0; k < COLS * ROWS; k++) {
            int idx = row * COLS + k;
            ItemStack s = idx < all.size() ? all.get(idx) : ItemStack.EMPTY;
            page.add(s);
            JsonObject o = new JsonObject();
            o.addProperty("id", s.isEmpty() ? "" : BuiltInRegistries.ITEM.getKey(s.getItem()).toString());
            o.addProperty("name", s.isEmpty() ? "" : s.getHoverName().getString());
            o.addProperty("count", s.getCount());
            a.add(o);
        }
        textures.publish(PAGE_SHEET, ItemIcons.render(mc, page), false);
        JsonObject r = new JsonObject();
        r.addProperty("tab", tab); r.addProperty("row", row); r.addProperty("rows", rows); r.addProperty("maxRow", maxRow);
        r.addProperty("total", all.size()); r.addProperty("sheet", PAGE_SHEET);
        r.add("items", a);
        return r;
    }

    /** Click on a tab item: carry a full stack (left) or one item (right); with something carried, delete it. */
    public static String pick(Minecraft mc, int tab, int row, String query, int cell, int button) {
        InventoryMenu menu = mc.player.inventoryMenu;
        if (!menu.getCarried().isEmpty()) { menu.setCarried(ItemStack.EMPTY); return "deleted carried stack"; }
        List<ItemStack> all = items(mc, tab, query);
        int idx = row * COLS + cell;
        if (cell < 0 || cell >= COLS * ROWS || idx >= all.size()) return "empty cell";
        ItemStack s = all.get(idx).copy();
        s.setCount(button == 1 ? 1 : s.getMaxStackSize());
        menu.setCarried(s);
        return "carrying " + s.getCount() + " " + BuiltInRegistries.ITEM.getKey(s.getItem());
    }

    /** Click on a player slot (InventoryMenu index 5..45) with vanilla pickup/place/merge/swap semantics. */
    public static String slot(Minecraft mc, int slot, int button) {
        LocalPlayer p = mc.player;
        InventoryMenu menu = p.inventoryMenu;
        if (slot < 5 || slot > 45) return "invalid slot";
        ItemStack in = menu.getSlot(slot).getItem().copy();
        ItemStack carried = menu.getCarried().copy();
        ItemStack newSlot, newCarried;
        if (carried.isEmpty()) {
            if (in.isEmpty()) return "nothing";
            if (button == 1) { // take half (rounded up), like vanilla PICKUP right click
                int take = (in.getCount() + 1) / 2;
                newCarried = in.copyWithCount(take);
                newSlot = in.getCount() - take > 0 ? in.copyWithCount(in.getCount() - take) : ItemStack.EMPTY;
            } else { newCarried = in; newSlot = ItemStack.EMPTY; }
        } else if (!in.isEmpty() && ItemStack.isSameItemSameTags(in, carried) && in.getCount() < in.getMaxStackSize()) {
            int move = button == 1 ? 1 : Math.min(carried.getCount(), in.getMaxStackSize() - in.getCount());
            newSlot = in.copyWithCount(in.getCount() + move);
            newCarried = carried.getCount() - move > 0 ? carried.copyWithCount(carried.getCount() - move) : ItemStack.EMPTY;
        } else if (in.isEmpty() && button == 1) {
            newSlot = carried.copyWithCount(1);
            newCarried = carried.getCount() > 1 ? carried.copyWithCount(carried.getCount() - 1) : ItemStack.EMPTY;
        } else { newSlot = carried; newCarried = in; } // place / swap
        menu.getSlot(slot).set(newSlot);
        mc.gameMode.handleCreativeModeItemAdd(newSlot, slot);
        menu.setCarried(newCarried);
        return "slot " + slot + " = " + (newSlot.isEmpty() ? "empty" : newSlot.getCount() + " " + BuiltInRegistries.ITEM.getKey(newSlot.getItem()));
    }

    /** The creative "Destroy Item" slot: clears the carried stack; shift-click clears the whole inventory. */
    public static String destroy(Minecraft mc, boolean all) {
        InventoryMenu menu = mc.player.inventoryMenu;
        menu.setCarried(ItemStack.EMPTY);
        if (all) for (int s = 5; s <= 45; s++) { menu.getSlot(s).set(ItemStack.EMPTY); mc.gameMode.handleCreativeModeItemAdd(ItemStack.EMPTY, s); }
        return all ? "cleared inventory" : "destroyed carried stack";
    }
}
