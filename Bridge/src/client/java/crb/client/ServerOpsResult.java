package crb.client;

import com.google.gson.JsonObject;

/** RESULT envelope helpers: echoes the command id/op so the host can match replies. */
final class ServerOpsResult {
    static JsonObject wrap(JsonObject cmd, JsonObject r) {
        if (cmd.has("id")) r.add("id", cmd.get("id"));
        if (cmd.has("op")) r.add("op", cmd.get("op"));
        return r;
    }
    static JsonObject ok(JsonObject cmd, String m) { JsonObject r = new JsonObject(); r.addProperty("ok", true); r.addProperty("message", m); return wrap(cmd, r); }
    static JsonObject error(JsonObject cmd, String m) { JsonObject r = new JsonObject(); r.addProperty("ok", false); r.addProperty("message", m); return wrap(cmd, r); }
}
