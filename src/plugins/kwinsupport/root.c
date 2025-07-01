#define _GNU_SOURCE

#include <plugins.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-bus-vtable.h>
#include <unistd.h>

#include <uthash.h>

#include <sys/mman.h>

#define PLUGIN_VERSION "0.0.0"
#define KWIN_PLUGIN_NAME "WlShimeji.KWinSupport"

extern const unsigned char _binary_data_js_start[];
extern const unsigned char _binary_data_js_end[];

int32_t script_fd = -1;

static sd_bus* dbus = NULL;


plugin_t* self = NULL;

static int cursor_handler(sd_bus_message *msg, void *udata, sd_bus_error* ret_err);
static int active_ie(sd_bus_message *msg, void* udata, sd_bus_error* ret_err);

static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD(
        "cursor", "ii", "", cursor_handler, 0
    ),
    SD_BUS_METHOD(
        "active_ie", "biiii", "", active_ie, 0
    ),
    SD_BUS_VTABLE_END
};

set_cursor_pos_func set_cursor_cb = NULL;
set_active_ie_func set_ie_cb = NULL;

static int register_kwin_plugin(const char* name) // Returns script_id or -1 on failure
{
    if (!dbus) {
        WARN("[KWinSupport] register_kwin_plugin called with unitialized dbus");
        return -1;
    }

    script_fd = memfd_create("KwinSupport.js", 0);
    write(script_fd, _binary_data_js_start, _binary_data_js_end - _binary_data_js_start);

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
        "ss",
        tmpfile_path,
        name
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

    if (retcode < 0 && script_fd != -1) {
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

int wl_shimeji_plugin_init(plugin_t* _self, set_cursor_pos_func set_cursor, set_active_ie_func set_ie)
{
    self = _self;
    set_cursor_cb = set_cursor;
    set_ie_cb = set_ie;

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

    int32_t script_id = register_kwin_plugin(KWIN_PLUGIN_NAME);
    if (script_id < 0) {
        WARN("[KWinSupport] Failed to register KWin plugin");
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
    close(script_fd);
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
    sd_bus_message_read(msg, "biiii", &is_active, &x, &y, &width, &height);
    set_ie_cb(is_active, x, y, width, height);

    sd_bus_reply_method_return(msg, "");

    return 1;
}
