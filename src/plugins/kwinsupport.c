#include "../mascot.h"
#include "../plugins.h"
#include "../environment.h"
#include "../master_header.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <systemd/sd-bus-protocol.h>
#include <systemd/sd-bus.h>

#define MAX_IE_OBJECTS 256
#define PLUGIN_VERSION "0.0.0"
// In pixels
#define MOVEMENT_THRESHOLD 50

enum plugin_execution_result initialize_ie (struct ie_object* ie, const char* serial, uint32_t id);
enum plugin_execution_result deinitialize_ie (struct ie_object* ie);
enum plugin_execution_result execute(struct ie_object* ie, int32_t* x, int32_t* y, uint32_t tick);
enum plugin_execution_result execute_ie_attach_mascot (struct ie_object* ie, struct mascot* mascot); // ie, mascot
enum plugin_execution_result execute_ie_detach_mascot (struct ie_object* ie, struct mascot* mascot); // ie, mascot
enum plugin_execution_result execute_ie_move (struct ie_object* ie, int32_t x, int32_t y);
enum plugin_execution_result execute_throw_ie(struct ie_object* ie, float x_velocity, float y_velocity, float gravity, uint32_t start_tick);
enum plugin_execution_result execute_stop_ie(struct ie_object* ie);
enum plugin_initialization_result init(uint32_t caps, const char** error_message_return_pointer);
void deinit();

struct plugin plugin_info = {
    .name = "KWinSupport",
    .description = "Support for KWin",
    .version = PLUGIN_VERSION,
    .provides = PLUGIN_PROVIDES_IE_POSITION | PLUGIN_PROVIDES_CURSOR_POSITION | PLUGIN_PROVIDES_IE_MOVE,
    .init = init,
    .deinit = deinit,
    .initialize_ie = initialize_ie,
    .deinitialize_ie = deinitialize_ie,
    .execute = execute,
    .execute_ie_attach_mascot = execute_ie_attach_mascot,
    .execute_ie_detach_mascot = execute_ie_detach_mascot,
    .execute_ie_move = execute_ie_move,
    .execute_throw_ie = execute_throw_ie,
    .execute_stop_ie = execute_stop_ie
};

struct {
    sd_bus *bus;
    const char *unique_name;

    int32_t script_id;

    // Allocated ie objects
    struct ie_object *ies[MAX_IE_OBJECTS];
    uint8_t ie_index;

    uint32_t current_tick;

} plugin_storage = {0};

struct mascot_store {
    struct mascot** mascot;
    uint8_t* states;
    size_t capacity;
    size_t used_count;
};

struct ie_object_internal_data {
    struct ie_object *ie;
    struct mascot_store attached_mascots;

    // KWin specific data
    char window_id[64];
    char output_id[64];

    int32_t pending_x;
    int32_t pending_y;
    int32_t pending_width;
    int32_t pending_height;

    bool pending_active;
    bool pending_fullscreen;

    bool pending_commit;

    uint64_t version;
};

bool mascot_store_init(struct mascot_store *store, size_t capacity)
{
    store->mascot = calloc(capacity, sizeof(struct mascot*));
    store->states = calloc(capacity, sizeof(uint8_t));
    store->capacity = capacity;
    store->used_count = 0;
    if (!store->mascot || !store->states) {
        free(store->mascot);
        free(store->states);
        return false;
    }
    return true;
}

bool mascot_store_add(struct mascot_store *store, struct mascot *mascot)
{
    // Grow the store if needed
    if (store->used_count == store->capacity) {
        size_t new_capacity = store->capacity * 2;
        struct mascot **new_mascot = realloc(store->mascot, new_capacity * sizeof(struct mascot*));
        uint8_t *new_states = realloc(store->states, new_capacity * sizeof(uint8_t));
        if (!new_mascot || !new_states) {
            return false;
        }
        // Zero out the new memory
        memset(new_mascot + store->capacity, 0, (new_capacity - store->capacity) * sizeof(struct mascot*));
    }

    // Find the first empty slot
    for (size_t i = 0; i < store->capacity; i++) {
        if (!store->states[i]) {
            store->mascot[i] = mascot;
            store->states[i] = 1;
            store->used_count++;
            return true;
        }
    }
    return false;
}

bool mascot_store_remove(struct mascot_store *store, struct mascot *mascot)
{
    for (size_t i = 0; i < store->capacity; i++) {
        if (store->states[i] && store->mascot[i] == mascot) {
            store->mascot[i] = NULL;
            store->states[i] = 0;
            store->used_count--;
            return true;
        }
    }
    return false;
}

size_t mascot_store_find(struct mascot_store *store, struct mascot* mascot)
{
    for (size_t i = 0; i < store->capacity; i++) {
        if (store->states[i] && store->mascot[i] == mascot) {
            return i;
        }
    }
    return SIZE_MAX;
}

struct mascot* mascot_store_get(struct mascot_store *store, size_t index)
{
    if (index >= store->capacity) {
        return NULL;
    }
    return store->mascot[index];
}

void mascot_store_free(struct mascot_store *store)
{
    free(store->mascot);
    free(store->states);
}

struct mascot* mascot_store_iterate(struct mascot_store *store, size_t *index)
{
    struct mascot *mascot = NULL;
    for (size_t i = *index; i < store->capacity; i++) {
        if (store->states[i]) {
            mascot = store->mascot[i];
            *index = i+1;
            break;
        }
    }
    return mascot;
}

// ------------------------------

enum plugin_initialization_result init(uint32_t caps, const char** error_message_return_pointer)
{
    plugin_storage.bus = NULL;
    plugin_storage.unique_name = NULL;
    plugin_storage.current_tick = -1;

    // Connect to the bus
    int r = sd_bus_open_user(&plugin_storage.bus);
    if (r < 0) {
        *error_message_return_pointer = "Failed to connect to the session bus";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    // Get the unique name
    r = sd_bus_get_unique_name(plugin_storage.bus, &plugin_storage.unique_name);
    if (r < 0) {
        *error_message_return_pointer = "Failed to get the unique name";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    // Now ensure that KWin scripting api is present
    char **names = NULL;
    r = sd_bus_list_names(plugin_storage.bus, &names, NULL);
    if (r < 0) {
        *error_message_return_pointer = "Failed to list names";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    bool found = false;
    for (char **name = names; *name; name++) {
        if (strcmp(*name, "org.kde.KWin") == 0) {
            found = true;
            break;
        }
    }
    free(names);

    if (!found) {
        *error_message_return_pointer = "org.kde.KWin is not on dbus";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }


    char kwin_script[4096] = {0};
    snprintf(kwin_script, sizeof(kwin_script),
    "var recipient_name = \"%s\";\n"
    "var active_window = null;\n"
    "function windowActivated(window) {\n"
    "    if (!window) return;\n"
    "    if (window.normalWindow) {\n"
    "        if (active_window != null) {\n"
    "           if (active_window.internalId == window.internalId) {\n"
    "               return;\n"
    "           }\n"
    "           active_window.minimizedChanged?.disconnect(windowMinimized);\n"
    "           active_window.fullScreenChanged?.disconnect(windowFullScreen);\n"
    "           active_window.frameGeometryChanged?.disconnect(windowGeometry);\n"
    "           active_window.maximizedChanged?.disconnect(windowMaximized);\n"
    "           active_window.interactiveMoveResizeStarted?.disconnect(moveStarted);\n"
    "           active_window.interactiveMoveResizeFinished?.disconnect(moveFinished);\n"
    "        }\n"
    "        active_window = window;\n"
    "        active_window?.minimizedChanged.connect(windowMinimized);\n"
    "        active_window?.fullScreenChanged.connect(windowFullScreen);\n"
    "        active_window?.frameGeometryChanged.connect(windowGeometry);\n"
    "        active_window?.maximizedChanged.connect(windowMaximized);\n"
    "        active_window?.interactiveMoveResizeStarted.connect(moveStarted);\n"
    "        active_window?.interactiveMoveResizeFinished.connect(moveFinished);\n"
    "        var window_id = window.internalId;\n"
    "        var output_id = window.output.name;\n"
    "        callDBus(recipient_name, '/', '', 'windowActivated', window_id.toString(), output_id);\n"
    "        windowGeometry(window.frameGeometry);\n"
    "        windowMinimized();\n"
    "        windowFullScreen();\n"
    "    }\n"
    "}\n"
    "function windowMinimized() {\n"
    "    callDBus(recipient_name, '/', '', 'windowMinimized', active_window.internalId.toString(), active_window.minimized);\n"
    "}\n"
    "function windowFullScreen() {\n"
    "    callDBus(recipient_name, '/', '', 'windowFullScreen', active_window.internalId.toString(), active_window.fullScreen);\n"
    "}\n"
    "function windowMaximized() {\n"
    "    windowGeometry(active_window.frameGeometry);\n"
    "};\n"
    "function deactivate() {\n"
    "   callDBus(recipient_name, '/', '', 'deactivate', active_window.internalId.toString() ? active_window.internalId.toString() : '0');\n"
    "};\n"
    "function windowGeometry(new_geometry) {\n"
    "    var x = Math.floor(new_geometry.x);\n"
    "    var y = Math.floor(new_geometry.y);\n"
    "    var w = Math.floor(new_geometry.width);\n"
    "    var h = Math.floor(new_geometry.height);\n"
    "    callDBus(recipient_name, '/', '', 'windowGeometry', active_window.internalId.toString(), x, y, w, h);\n"
    "};\n"
    "function cursorPoseChanged() {\n"
    "    var x = Math.floor(workspace.cursorPos.x);\n"
    "    var y = Math.floor(workspace.cursorPos.y);\n"
    "    callDBus(recipient_name, '/', '', 'cursorPoseChanged', x, y);\n"
    "};\n"
    "function currentDesktopChanged(old) {\n"
    "   var window = workspace.activeWindow;\n"
    "   if (!window) {\n"
    "       deactivate();\n"
    "       return;\n"
    "   }\n"
    "   windowActivated(window);\n"
    "};\n"
    "function moveStarted() {\n"
    "   callDBus(recipient_name, '/', '', 'moveStarted', active_window.internalId.toString());\n"
    "};\n"
    "function moveFinished() {\n"
    "   callDBus(recipient_name, '/', '', 'moveFinished', active_window.internalId.toString());\n"
    "};\n"
    "workspace.cursorPosChanged.connect(cursorPoseChanged);\n"
    "workspace.windowActivated.connect(windowActivated);\n"
    "workspace.currentDesktopChanged.connect(currentDesktopChanged);\n"
    "windowActivated(workspace.activeWindow);\n",
    plugin_storage.unique_name);

    // Write script to tmp file
    char tmpname [] = "/tmp/kwin_script_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd == -1) {
        *error_message_return_pointer = "Failed to create temporary file";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    write(fd, kwin_script, strlen(kwin_script));
    close(fd);

    // Load script
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    r = sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           "/Scripting",
                           "org.kde.kwin.Scripting",
                           "loadScript",
                           &error,
                           &reply,
                           "s",
                           tmpname);
    if (r < 0) {
        *error_message_return_pointer = "Failed to load script";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    // unlink(tmpname);

    // Check for error then get script id
    int32_t script_id = -1;
    if (error.name != NULL) {
        *error_message_return_pointer = error.message;
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    r = sd_bus_message_read(reply, "i", &script_id);
    if (r < 0) {
        *error_message_return_pointer = "Failed to read script id";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    // Store script id
    plugin_storage.script_id = script_id;

    DEBUG("[KWinSupport] Script id: %d\n", script_id);

    // Run the script (do not wait for reply)
    char script_path[128] = {0};
    snprintf(script_path, 128, "/Scripting/Script%d", script_id);
    r = sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           script_path,
                           "org.kde.kwin.Script",
                           "run",
                           &error,
                           NULL,
                           "");

    if (r < 0) {
        *error_message_return_pointer = "Failed to run script";
        return PLUGIN_INIT_BAD_ENVIRONMENT;
    }

    unlink(tmpname);


    plugin_info.effective_caps = caps & plugin_info.provides;

    return PLUGIN_INIT_OK;
}

void deinit()
{
    if (plugin_storage.script_id != -1) {
        char script_path[128] = {0};
        snprintf(script_path, 128, "/Scripting/Script%d", plugin_storage.script_id);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           script_path,
                           "org.kde.kwin.Script",
                           "stop",
                           &error,
                           NULL,
                           "");
    }
    sd_bus_close(plugin_storage.bus);

    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
        if (plugin_storage.ies[i] != NULL) {
            deinitialize_ie(plugin_storage.ies[i]);
        }
    }

    DEBUG("[KWinSupport] Deinitialized");

    free((void*)plugin_storage.unique_name);
}

enum plugin_execution_result execute(struct ie_object* ie, int32_t* x, int32_t* y, uint32_t tick)
{
    char window_id [64] = {0};

    int sdresult;
    // Process dbus
    sd_bus_message *message = NULL;
    if (plugin_storage.current_tick != tick) {
        plugin_storage.current_tick = tick;
        while (sd_bus_process(plugin_storage.bus, &message) > 0) {
            if (!message) continue;
            uint8_t message_type = 0;
            sdresult = sd_bus_message_get_type(message, &message_type);
            if (sdresult < 0) {
                WARN("[KWINSUPPORT] Failed to get message type: %s\n", strerror(-sdresult));
                return PLUGIN_EXEC_ERROR;
            }
            const char *member = sd_bus_message_get_member(message);
            const char *path = sd_bus_message_get_path(message);
            const char *interface = sd_bus_message_get_interface(message);
            const char *destination = sd_bus_message_get_destination(message);

            DEBUG("[KWINSUPPORT] Received method call: %s %s %s %s", member, path, interface, destination);
            if (message_type == SD_BUS_MESSAGE_METHOD_CALL) {                // Handle new active window
                if (strcmp(member, "windowActivated") == 0) {
                    const char *window_id = NULL;
                    const char *output_id = NULL;
                    sdresult = sd_bus_message_read(message, "ss", &window_id, &output_id);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read windowActivated message: %s", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->output_id, output_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s activated for unknown output: %s\n", window_id, output_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Detach all mascots from the ie
                    struct mascot* mascot = NULL;
                    size_t mascot_index = 0;
                    while ((mascot = mascot_store_iterate(&ie_data->attached_mascots, &mascot_index)) != NULL) {
                        execute_ie_detach_mascot(ie, mascot);
                        mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    }

                    // Change parameters of the ie
                    strncpy(ie_data->window_id, window_id, 64);
                    ie->x_velocity = 0;
                    ie->y_velocity = 0;
                    ie->held_by = NULL;
                    ie->state = IE_STATE_IDLE;
                    ie_data->pending_commit = true;
                }
                else if (strcmp(member, "windowGeometry") == 0) {
                    const char *window_id = NULL;
                    int32_t x, y;
                    int32_t w, h;

                    sdresult = sd_bus_message_read(message, "siiii", &window_id, &x, &y, &w, &h);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read geometry message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    if (ie->state == IE_STATE_THROWN) {
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s changed geometry for unknown output\n", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    ie_data->pending_x = x;
                    ie_data->pending_y = y;
                    ie_data->pending_width = w;
                    ie_data->pending_height = h;
                    ie_data->pending_active = !(x == 0 && y == 0 && w == (int32_t)environment_workarea_width(ie->environment) && h == (int32_t)environment_workarea_height(ie->environment));
                    ie_data->pending_commit = true;
                }
                else if (strcmp(member, "windowMinimized") == 0) {
                    const char *window_id = NULL;
                    int32_t is_minimized;

                    sdresult = sd_bus_message_read(message, "sb", &window_id, &is_minimized);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read windowMinimized message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s minimized for unknown output\n", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Change parameters of the ie
                    ie_data->pending_active = !is_minimized;
                    ie_data->pending_commit = true;
                }
                else if (strcmp(member, "windowFullScreen") == 0) {
                    const char *window_id = NULL;
                    int32_t is_full_screen;

                    sdresult = sd_bus_message_read(message, "sb", &window_id, &is_full_screen);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read windowFullscreen message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s fullscreen changed for unknown output\n", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Change parameters of the ie
                    ie_data->pending_fullscreen = is_full_screen;
                    ie_data->pending_active = !is_full_screen;
                    ie_data->pending_commit = true;
                }
                else if (strcmp(member, "cursorPoseChanged") == 0) {
                    sdresult = sd_bus_message_read(message, "ii", x, y);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read cursorPoseChanged message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }
                }
                else if (strcmp(member, "deactivate") == 0) {
                    sdresult = sd_bus_message_read(message, "s", x, y);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read deactivate message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s fullscreen changed for unknown output\n", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    ie_data->pending_active = false;
                    ie_data->pending_fullscreen = false;
                    ie_data->window_id[0] = 0;
                    ie_data->pending_commit = true;
                    ie->state = IE_STATE_IDLE;
                }
                else if (strcmp(member, "moveStarted") == 0) {
                    const char *window_id = NULL;

                    sdresult = sd_bus_message_read(message, "s", &window_id);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read moveStarted message: %s\n", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s fullscreen changed for unknown output", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    ie->state = IE_STATE_MOVED;
                }
                else if (strcmp(member, "moveFinished") == 0) {
                    const char *window_id = NULL;

                    sdresult = sd_bus_message_read(message, "s", &window_id);
                    if (sdresult < 0) {
                        WARN("[KWINSUPPORT] Failed to read moveStarted message: %s", strerror(-sdresult));
                        sd_bus_message_unref(message);
                        continue;
                    }

                    // Find the ie corresponding to the output
                    struct ie_object* ie = NULL;
                    struct ie_object_internal_data* ie_data = NULL;
                    for (int i = 0; i < MAX_IE_OBJECTS; i++) {
                        if (plugin_storage.ies[i] != NULL) {
                            struct ie_object* local_ie = plugin_storage.ies[i];
                            ie_data = (struct ie_object_internal_data*)local_ie->data;
                            if (strcmp(ie_data->window_id, window_id) == 0) {
                                ie = local_ie;
                                break;
                            }
                        }
                    }
                    if (ie == NULL) {
                        WARN("[KWINSUPPORT] Window %s fullscreen changed for unknown output", window_id);
                        sd_bus_message_unref(message);
                        continue;
                    }

                    ie->state = IE_STATE_IDLE;
                }
            }
            sd_bus_message_unref(message);
        }
    }

    // If called on ie
    if (ie) {
        struct ie_object_internal_data* ie_data = (struct ie_object_internal_data*)ie->data;

        if (ie->state == IE_STATE_THROWN) {
            // Move window
            float time = (tick - ie->reference_tick);
            int32_t new_x = ie->x + ie->x_velocity;
            int32_t new_y = ie->y + ie->y_velocity + (float)(time * ie->gravity);
            environment_ie_move(ie->environment, new_x, new_y);
        }

        if (!ie_data->pending_commit) {
            return PLUGIN_EXEC_OK;
        }
        // Found how much each mascots should move, and if that value is greater than MOVEMENT_THRESHOLD, detach the mascot
        size_t mascot_index = 0;
        struct mascot* mascot = NULL;
        while ((mascot = mascot_store_iterate(&ie_data->attached_mascots, &mascot_index))) {
            if (mascot == NULL) continue;

            if (ie_data->pending_active == false) {
                execute_ie_detach_mascot(ie, mascot);
                mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                continue;
            }

            if (ie_data->pending_fullscreen) {
                execute_ie_detach_mascot(ie, mascot);
                mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                continue;
            }

            int32_t mx = mascot->X->value.i;
            int32_t my = mascot->Y->value.i;
            int32_t new_x = mx;
            int32_t new_y = my;
            enum environment_border_type border = environment_get_border_type(mascot->environment, mx, my);
            if (border == environment_border_type_floor) {

                if (abs(ie_data->pending_y - ie->y) <= MOVEMENT_THRESHOLD) {
                    new_y = mascot_screen_y_to_mascot_y(mascot, ie_data->pending_y);
                }
                else {
                    execute_ie_detach_mascot(ie, mascot);
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    continue;
                }

                if (abs(ie_data->pending_x - ie->x) <= MOVEMENT_THRESHOLD) {
                    new_x += ie_data->pending_x - ie->x;
                }
                else {
                    execute_ie_detach_mascot(ie, mascot);
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    continue;
                }
            }
            else if (border == environment_border_type_wall) {
                // Find left or right wall
                bool left_wall = mx == ie->x;


                if (abs(ie_data->pending_y - ie->y) <= MOVEMENT_THRESHOLD) {
                    new_y -= ie_data->pending_y - ie->y;
                }
                else {
                    execute_ie_detach_mascot(ie, mascot);
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    continue;
                }

                // Handle left wall movement
                if (left_wall) {
                    if (abs(ie_data->pending_x - ie->x) <= MOVEMENT_THRESHOLD) {
                        new_x = ie_data->pending_x;
                    }
                    else {
                        execute_ie_detach_mascot(ie, mascot);
                        mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                        continue;
                    }
                }

                // Handle right wall movement
                else {
                    int32_t right_x = ie_data->pending_x + ie_data->pending_width;
                    if (abs(right_x - mx) <= MOVEMENT_THRESHOLD) {
                        new_x = right_x;
                    }
                    else {
                        execute_ie_detach_mascot(ie, mascot);
                        mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                        continue;
                    }
                }
            } else if (border == environment_border_type_ceiling) {
                int32_t bottom_y = ie_data->pending_y + ie_data->pending_height;

                if (abs(bottom_y - my) <= MOVEMENT_THRESHOLD) {
                    new_y = bottom_y;
                }
                else {
                    execute_ie_detach_mascot(ie, mascot);
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    continue;
                }

                if (abs(ie->x - ie_data->pending_x) <= MOVEMENT_THRESHOLD) {
                    new_x += ie_data->pending_x - ie->x;
                }
                else {
                    execute_ie_detach_mascot(ie, mascot);
                    mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
                    continue;
                }

            } else {
                // ??? Detach.
                execute_ie_detach_mascot(ie, mascot);
                mascot_set_behavior(mascot, mascot_fall_behavior(mascot));
            }

            // Move mascots
            mascot_moved(mascot, new_x, new_y);
            environment_subsurface_set_position(mascot->subsurface, new_x, mascot_screen_y_to_mascot_y(mascot, new_y));
        }

        ie->x = ie_data->pending_x;
        ie->y = ie_data->pending_y;
        ie->width = ie_data->pending_width;
        ie->height = ie_data->pending_height;
        ie->active = ie_data->pending_active;
        ie->fullscreen = ie_data->pending_fullscreen;
        ie_data->pending_commit = false;
    }

    return PLUGIN_EXEC_OK;
}

enum plugin_execution_result initialize_ie (struct ie_object* ie, const char* name, uint32_t id)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!name) return PLUGIN_EXEC_NULLPTR;
    UNUSED(id);

    struct ie_object_internal_data* ie_data = calloc(1,sizeof(struct ie_object_internal_data));
    if (!ie_data) return PLUGIN_EXEC_ERROR;

    ie->data = ie_data;
    ie->type = IE_TYPE_WINDOW;
    ie->state = IE_STATE_IDLE;
    ie->x = 0;
    ie->y = 0;
    ie->width = 0;
    ie->height = 0;
    ie->active = false;
    ie->fullscreen = false;

    ie->held_by = NULL;
    ie->parent_plugin = &plugin_info;

    strncpy(ie_data->output_id, name, sizeof(ie_data->output_id));

    mascot_store_init(&ie_data->attached_mascots, 256);

    plugin_storage.ies[plugin_storage.ie_index++] = ie;

    return PLUGIN_EXEC_OK;
}

enum plugin_execution_result deinitialize_ie (struct ie_object* ie)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;

    struct ie_object_internal_data* ie_data = (struct ie_object_internal_data*)ie->data;
    if (ie_data) {
        mascot_store_free(&ie_data->attached_mascots);
        free(ie_data);
    }

    for (size_t i = 0; i < plugin_storage.ie_index; i++) {
        if (plugin_storage.ies[i] == ie) {
            plugin_storage.ies[i] = NULL;
            break;
        }
    }

    return PLUGIN_EXEC_OK;
}
enum plugin_execution_result execute_ie_attach_mascot (struct ie_object* ie, struct mascot* mascot)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!mascot) return PLUGIN_EXEC_NULLPTR;

    DEBUG("Attaching mascot %d to ie %p", mascot->id, ie);

    struct ie_object_internal_data* ie_data = (struct ie_object_internal_data*)ie->data;
    if (!ie_data) return PLUGIN_EXEC_NULLPTR;

    if (!ie->active) return PLUGIN_EXEC_ERROR;

    mascot->associated_ie = ie;

    bool res = mascot_store_add(&ie_data->attached_mascots, mascot);

    return res ? PLUGIN_EXEC_OK : PLUGIN_EXEC_ERROR;
}

enum plugin_execution_result execute_ie_detach_mascot (struct ie_object* ie, struct mascot* mascot)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!mascot) return PLUGIN_EXEC_NULLPTR;

    DEBUG("Detaching mascot %d to ie %p", mascot->id, ie);

    struct ie_object_internal_data* ie_data = (struct ie_object_internal_data*)ie->data;
    if (!ie_data) return PLUGIN_EXEC_NULLPTR;

    if (!mascot->associated_ie) return PLUGIN_EXEC_OK;

    mascot->associated_ie = NULL;
    bool res = mascot_store_remove(&ie_data->attached_mascots, mascot);

    return res ? PLUGIN_EXEC_OK : PLUGIN_EXEC_ERROR;
}

enum plugin_execution_result execute_ie_move (struct ie_object* ie, int32_t x, int32_t y)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;

    struct ie_object_internal_data* ie_data = ie->data;
    if (!ie_data) return PLUGIN_EXEC_UNKNOWN_ERROR;

    char script[2048] = {0};

    INFO("[KWinSupport] Moving window %s to %d, %d", ie_data->window_id, x, y);

    snprintf(script, sizeof(script),
        "var window = workspace.stackingOrder.find(client => client.internalId.toString() == \"%s\");\n"
        "if (window) {\n"
        "    window.frameGeometry = { x: %d, y: %d, width: window.frameGeometry.width, height: window.frameGeometry.height };\n"
        "}\n",
        ie_data->window_id, x, y);

    int r;

    // Write script to tmp file
    char tmpname [] = "/tmp/kwin_script_XXXXXX";
    int fd = mkstemp(tmpname);
    if (fd == -1) {
        WARN("[KWinSupport] Failed to create temporary file for script");
        return PLUGIN_EXEC_ERROR;
    }

    write(fd, script, strlen(script));
    close(fd);

    // Load script
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    r = sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           "/Scripting",
                           "org.kde.kwin.Scripting",
                           "loadScript",
                           &error,
                           &reply,
                           "s",
                           tmpname);
    if (r < 0) {
        WARN("[KWinSupport] Failed to load script: %s", error.message);
        return PLUGIN_EXEC_ERROR;
    }

    // unlink(tmpname);

    // Check for error then get script id
    int32_t script_id = -1;
    r = sd_bus_message_read(reply, "i", &script_id);
    if (r < 0) {
        WARN("[KWinSupport] Failed to read script id");
        return PLUGIN_EXEC_ERROR;
    }

    // Store script id
    plugin_storage.script_id = script_id;

    DEBUG("[KWinSupport] Script id: %d\n", script_id);

    // Run the script (do not wait for reply)
    char script_path[128] = {0};
    snprintf(script_path, 128, "/Scripting/Script%d", script_id);
    r = sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           script_path,
                           "org.kde.kwin.Script",
                           "run",
                           &error,
                           NULL,
                           "");

    if (r < 0) {
        WARN("[KWinSupport] Failed to run script: %s", error.message);
        return PLUGIN_EXEC_ERROR;
    }

    // Stop script (effective unload)
    if (plugin_storage.script_id != -1) {
        char script_path[128] = {0};
        snprintf(script_path, 128, "/Scripting/Script%d", plugin_storage.script_id);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_call_method(plugin_storage.bus,
                           "org.kde.KWin",
                           script_path,
                           "org.kde.kwin.Script",
                           "stop",
                           &error,
                           NULL,
                           "");
    }

    ie_data->pending_x = x;
    ie_data->pending_y = y;
    ie_data->pending_commit = true;

    unlink(tmpname);

    return PLUGIN_EXEC_OK;
}

enum plugin_execution_result execute_throw_ie(struct ie_object* ie, float x_velocity, float y_velocity, float gravity, uint32_t start_tick)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;

    struct ie_object_internal_data* ie_data = ie->data;
    if (!ie_data) return PLUGIN_EXEC_UNKNOWN_ERROR;

    if (!ie->active || ie->fullscreen) return PLUGIN_EXEC_ERROR;

    ie->state = IE_STATE_THROWN;
    ie->x_velocity = x_velocity;
    ie->y_velocity = y_velocity;
    ie->gravity = gravity;

    ie->reference_tick = start_tick;

    return PLUGIN_EXEC_OK;
}

enum plugin_execution_result execute_stop_ie(struct ie_object *ie)
{
    if (!ie) return PLUGIN_EXEC_NULLPTR;

    if (ie->state == IE_STATE_THROWN) {
        ie->state = IE_STATE_IDLE;
        ie->x_velocity = 0;
        ie->y_velocity = 0;
        ie->gravity = 0;
        ie->reference_tick = 0;
    }

    return PLUGIN_EXEC_OK;
}
