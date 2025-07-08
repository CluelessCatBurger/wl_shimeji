#define _GNU_SOURCE

#include <plugins.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-bus-vtable.h>
#include <unistd.h>
#include <stddef.h>

#include <uthash.h>

#include <sys/mman.h>

#define PLUGIN_VERSION "0.0.0"
#define KWIN_PLUGIN_NAME "WlShimeji.KWinSupport"

extern const unsigned char _binary_data_js_start[];
extern const unsigned char _binary_data_js_end[];
extern const unsigned char _binary_move_js_start[];
extern const unsigned char _binary_move_js_end[];
extern const unsigned char _binary_restore_js_start[];
extern const unsigned char _binary_restore_js_end[];

static sd_bus* dbus = NULL;

struct fd_map {
    char name[128];
    int fd;
    UT_hash_handle hh;
};

struct fd_map* fd_map = NULL;

int move_id = -1;
int restore_id = -1;

int new_x = 0;
int new_y = 0;

plugin_t* self = NULL;

char window_internal_id[64] = {0};

static int cursor_handler(sd_bus_message *msg, void *udata, sd_bus_error* ret_err);
static int active_ie(sd_bus_message *msg, void* udata, sd_bus_error* ret_err);
static int new_ie_position(sd_bus_message *msg, void* udata, sd_bus_error* ret_err);
static int window_moved_hint(sd_bus_message *msg, void* udata, sd_bus_error* ret_err);

static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD(
        "cursor", "ii", "", cursor_handler, 0
    ),
    SD_BUS_METHOD(
        "active_ie", "sbiiii", "", active_ie, 0
    ),
    SD_BUS_METHOD(
        "new_ie_position", "", "sii", new_ie_position, 0
    ),
    SD_BUS_METHOD(
        "window_moved_hint", "", "", window_moved_hint, 0
    ),
    SD_BUS_VTABLE_END
};

set_cursor_pos_func set_cursor_cb = NULL;
set_active_ie_func set_ie_cb = NULL;
window_moved_hint_func window_moved_hint_cb = NULL;

static int register_kwin_plugin(const char* name, const unsigned char* src, int len) // Returns script_id or -1 on failure
{
    if (!dbus) {
        WARN("[KWinSupport] register_kwin_plugin called with unitialized dbus");
        return -1;
    }

    char script_name[64] = {0};
    snprintf(script_name, 63, "KWinSupport_%s.js", name);

    int script_fd = memfd_create(script_name, 0);
    write(script_fd, src, len);

    pid_t pid = getpid();

    char tmpfile_path[128] = {0};
    snprintf(tmpfile_path, sizeof(tmpfile_path), "/proc/%d/fd/%d", pid, script_fd);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    int retcode = sd_bus_call_method(
        dbus,
        "org.kde.KWin",
        "/Scripting",
        "org.kde.kwin.Scripting",
        "loadScript",
        &err,
        &reply,
        "s",
        tmpfile_path
    );

    if (retcode < 0) {
        WARN("[KWinSupport] Failed to load script, %s", strerror(-retcode));
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return -1;
    }

    int32_t script_id = -1;
    retcode = sd_bus_message_read(reply, "i", &script_id);
    if (retcode < 0) {
        WARN("[KWinSupport] Failed to read script ID, %s", strerror(-retcode));
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return -1;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    struct fd_map* fd_map_entry = NULL;
    HASH_FIND_STR(fd_map, name, fd_map_entry);
    if (!fd_map_entry) {
        fd_map_entry = calloc(1, sizeof(struct fd_map));
        fd_map_entry->fd = script_fd;
        memcpy(fd_map_entry->name, name, strlen(name) + 1);
        HASH_ADD_STR(fd_map, name, fd_map_entry);
    }
    else {
        close(fd_map_entry->fd);
        fd_map_entry->fd = script_fd;
    }

    return script_id;
}

static bool unregister_kwin_plugin(const char* name)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    int retcode = sd_bus_call_method(
        dbus,
        "org.kde.KWin",
        "/Scripting",
        "org.kde.kwin.Scripting",
        "unloadScript",
        &err,
        &reply,
        "s",
        name
    );

    if (retcode < 0) {
        WARN("[KWinSupportPlugin] Failed to unload script, %s", err.message);
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return false;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return true;
}

static bool unload_kwin_plugin(int script_id)
{

    char script_path[128];
    snprintf(script_path, 128, "/Scripting/Script%d", script_id);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    int retcode = sd_bus_call_method(
        dbus,
        "org.kde.KWin",
        script_path,
        "org.kde.kwin.Script",
        "stop",
        &err,
        &reply,
        NULL
    );

    if (retcode < 0) {
        WARN("[KWinSupportPlugin] Failed to unload script, %s", err.message);
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return false;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return true;
}

static bool start_kwin_script(int32_t script_id)
{
    char script_path[128];
    snprintf(script_path, 128, "/Scripting/Script%d", script_id);

    if (script_id < 0) {
        WARN("[KWinSupport] Failed to upload script to KWin, %s", strerror(-script_id));
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;

    int retcode = sd_bus_call_method(
        dbus,
        "org.kde.KWin",
        script_path,
        "org.kde.kwin.Script",
        "run",
        &err,
        &reply,
        ""
    );
    if (retcode < 0) {
        WARN("[KWinSupport] Failed to run script on KWin");
        return false;
    }
    return true;
}

int wl_shimeji_plugin_init(plugin_t* _self, set_cursor_pos_func set_cursor, set_active_ie_func set_ie, window_moved_hint_func window_moved_hint)
{
    self = _self;
    set_cursor_cb = set_cursor;
    set_ie_cb = set_ie;
    window_moved_hint_cb = window_moved_hint;

    const char* DE = getenv("XDG_CURRENT_DESKTOP");
    if (!DE) {
        WARN("[KWinSupport] XDG_CURRENT_DESKTOP environment variable not set");
        return -1;
    }

    if (strcmp("KDE", DE)) {
        WARN("[KWinSupport] Plugin requires KDE desktop environment");
        return -1;
    }

    if (!sd_bus_default_user(&dbus)) {
        WARN("[KWinSupport] Failed to connect to D-Bus");
        return -1;
    }

    const char* name = NULL;
    if (sd_bus_get_unique_name(dbus, &name)) {
        WARN("[KWinSupport] Failed to get unique name");
        return -1;
    }

    DEBUG("[KWinSupport] Requesting com.github.CluelessCatBurger.WlShimeji.KWinSupport");
    int retcode = sd_bus_request_name(dbus, "com.github.CluelessCatBurger.WlShimeji.KWinSupport", 0);
    if (retcode < 0) {
        WARN("[KWinSupport] Failed to request name, is wl_shimeji already running?");
        return -1;
    }

    retcode = sd_bus_add_object_vtable(dbus, NULL, "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", vtable, NULL);
    if (retcode < 0) {
        WARN("[KWinSupport] sd-bus: Failed to add object vtable");
        return -1;
    }

    // Try to unload old loaded script
    unregister_kwin_plugin(KWIN_PLUGIN_NAME);
    unregister_kwin_plugin(KWIN_PLUGIN_NAME ".move");
    unregister_kwin_plugin(KWIN_PLUGIN_NAME ".restore");

    int32_t script_id = register_kwin_plugin(KWIN_PLUGIN_NAME, _binary_data_js_start, _binary_data_js_end-_binary_data_js_start);
    if (script_id < 0) {
        WARN("[KWinSupport] Failed to register KWin plugin (data)");
        return -1;
    }

    bool started = start_kwin_script(script_id);
    if (!started) {
        WARN("[KWinSupport] Failed to start KWin script");
        return -1;
    }

    return plugin_init(self, "KWinSupport", "0.0.0", "CluelessCatBurger", "Window interaction support for KWin", version_to_i64(WL_SHIMEJI_PLUGIN_TARGET_VERSION));
}

int wl_shimeji_plugin_tick()
{
    int retcode = 0;
    while ((retcode = sd_bus_process(dbus, NULL) != 0)) {
        if (retcode < 0) {
            WARN("[KWinSupportPlugin] Failed to process D-Bus messages");
            return -1;
        }
    }
    return 0;
}

static int cursor_handler(sd_bus_message *msg, void *udata, sd_bus_error* ret_err)
{
    int32_t new_x = 0;
    int32_t new_y = 0;
    sd_bus_message_read(msg, "ii", &new_x, &new_y);
    (void)(udata);
    (void)(ret_err);
    set_cursor_cb(new_x, new_y);

    sd_bus_reply_method_return(msg, "");

    return 1;
};

int wl_shimeji_plugin_deinit() {
    bool unregistered = unregister_kwin_plugin(KWIN_PLUGIN_NAME);
    if (!unregistered) {
        WARN("[KWinSupportPlugin] Failed to unregister KWin plugin");
        return -1;
    }

    unregistered = unregister_kwin_plugin(KWIN_PLUGIN_NAME ".move");
    if (!unregistered) {
        WARN("[KWinSupportPlugin] Failed to unregister KWin plugin (move)");
        return -1;
    }

    unregistered = unregister_kwin_plugin(KWIN_PLUGIN_NAME ".restore");
    if (!unregistered) {
        WARN("[KWinSupportPlugin] Failed to unregister KWin plugin (restore)");
        return -1;
    }

    sd_bus_close(dbus);
    sd_bus_unref(dbus);

    INFO("[KWinSupportPlugin] Successfully deinitialized.");
    return 0;
}

static int active_ie(sd_bus_message *msg, void* udata, sd_bus_error* ret_err)
{
    (void)(udata);
    (void)(ret_err);

    int is_active = false;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    char* wid = NULL;
    sd_bus_message_read(msg, "sbiiii", &wid, &is_active, &x, &y, &width, &height);
    if (wid) memcpy(window_internal_id, wid, strlen(wid) + 1);
    else memset(window_internal_id, 0, sizeof(window_internal_id));
    set_ie_cb(is_active, x, y, width, height);


    sd_bus_reply_method_return(msg, "");

    return 1;
}

int wl_shimeji_plugin_move(plugin_t* self, int x, int y)
{
    if (!self) return -1;

    new_x = x;
    new_y = y;

    if (move_id >= 0) unload_kwin_plugin(move_id);
    move_id = register_kwin_plugin(KWIN_PLUGIN_NAME ".move", _binary_move_js_start, _binary_move_js_end-_binary_move_js_start);
    if (move_id < 0) {
        WARN("[KWinSupport] Failed to register KWin plugin (move)");
        return -1;
    }

    return start_kwin_script(move_id);
}

int wl_shimeji_plugin_restore(plugin_t* self)
{
    if (!self) return -1;

    // char script_path[128];
    // snprintf(script_path, 128, "/Scripting/Script%d", restore_id);

    // if (restore_id >= 0) {
    //     sd_bus_error err = SD_BUS_ERROR_NULL;
    //     sd_bus_message* reply = NULL;

    //     sd_bus_call_method(
    //         dbus,
    //         "org.kde.KWin",
    //         script_path,
    //         "org.kde.kwin.Script",
    //         "stop",
    //         &err,
    //         &reply,
    //         ""
    //     );
    // }

    if (restore_id >= 0) unload_kwin_plugin(restore_id);
    restore_id = register_kwin_plugin(KWIN_PLUGIN_NAME ".restore", _binary_restore_js_start, _binary_restore_js_end-_binary_restore_js_start);
    if (restore_id < 0) {
        WARN("[KWinSupport] Failed to register KWin plugin (restore)");
        return -1;
    }

    return start_kwin_script(restore_id);
}

static int new_ie_position(sd_bus_message *msg, void* udata, sd_bus_error* ret_err)
{
    (void)(udata);
    (void)(ret_err);

    int errcode = sd_bus_reply_method_return(msg, "sii", window_internal_id, new_x, new_y);
    return errcode;
}

static int window_moved_hint(sd_bus_message *msg, void* udata, sd_bus_error* ret_err)
{
    (void)(udata);
    (void)(ret_err);

    int errcode = sd_bus_reply_method_return(msg, "", window_internal_id, new_x, new_y);
    new_x = INT32_MIN;
    new_y = INT32_MIN;
    window_moved_hint_cb();

    return errcode;
}
