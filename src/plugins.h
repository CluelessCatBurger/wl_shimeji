/*
    plugin.h - wl_shimeji's plugin support

    Copyright (C) 2025  CluelessCatBurger <github.com/CluelessCatBurger>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PLUGINS_H
#define PLUGINS_H

struct plugin;
struct ie_object;

#include "mascot.h"
#include "environment.h"

#include <unistd.h>

#define PLUGIN_PROVIDES_CURSOR_POSITION 1
#define PLUGIN_PROVIDES_IE_POSITION 2
#define PLUGIN_PROVIDES_IE_MOVE 4

#define PLUGIN_IE_THROW_POLICY_NONE 0
#define PLUGIN_IE_THROW_POLICY_STOP_AT_BORDERS 1
#define PLUGIN_IE_THROW_POLICY_BOUNCE_AT_BORDERS 2
#define PLUGIN_IE_THROW_POLICY_LOOP 3
#define PLUGIN_IE_THROW_POLICY_CLOSE 4
#define PLUGIN_IE_THROW_POLICY_MINIMIZE 5
#define PLUGIN_IE_KEEP_OFFSCREEN 6

enum plugin_initialization_result {
    PLUGIN_INIT_OK,
    PLUGIN_INIT_BAD_ENVIRONMENT,
    PLUGIN_INIT_BAD_VERSION,
    PLUGIN_INIT_BAD_DESCRIPTION,
    PLUGIN_INIT_SEGFAULT,
    PLUGIN_INIT_UNKNOWN_ERROR,
    PLUGIN_INIT_NULLPTR
};

enum plugin_execution_result {
    PLUGIN_EXEC_OK,
    PLUGIN_EXEC_ERROR,
    PLUGIN_EXEC_SEGFAULT,
    PLUGIN_EXEC_UNKNOWN_ERROR,
    PLUGIN_EXEC_NULLPTR
};


struct ie_object {
    enum {
        IE_TYPE_WINDOW
    } type;

    enum {
        IE_STATE_IDLE,
        IE_STATE_MOVED,
        IE_STATE_THROWN
    } state;

    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    uint32_t reference_tick;

    float x_velocity;
    float y_velocity;
    float gravity;

    bool active;
    bool fullscreen;

    struct mascot* held_by;

    environment_t* environment;
    struct plugin* parent_plugin;
    void* data;
};

struct plugin {
    const char* name;
    const char* description;
    const char* author;
    const char* version;
    const char* website;
    const char* license;
    const char* filename;

    enum plugin_initialization_result (*init)(uint32_t, const char**); // capabilities, error_message
    void (*deinit)(void);

    uint32_t provides;
    uint32_t effective_caps;

    enum plugin_execution_result (*execute)(struct ie_object*, int32_t*, int32_t*, uint32_t); // ie, mouse_x, mouse_y, tick
    enum plugin_execution_result (*execute_ie_move)(struct ie_object* ie, int32_t x, int32_t y); // ie, x, y
    enum plugin_execution_result (*execute_ie_attach_mascot)(struct ie_object* ie, struct mascot* mascot); // ie, mascot
    enum plugin_execution_result (*execute_ie_detach_mascot)(struct ie_object* ie, struct mascot* mascot); // ie, mascot
    enum plugin_execution_result (*execute_throw_ie)(struct ie_object* ie, float x_velocity, float y_velocity, float gravity, uint32_t start_tick);
    enum plugin_execution_result (*execute_stop_ie)(struct ie_object* ie); // ie
    enum plugin_execution_result (*execute_ie_throw_policy)(int policy);

    // Deactivate current IE and restore offscreen windows
    enum plugin_execution_result (*execute_deactivate_ie)(struct ie_object* ie);
    enum plugin_execution_result (*execute_restore_ies)(void);

    enum plugin_execution_result (*initialize_ie)(struct ie_object* ie, const char*, uint32_t id);
    enum plugin_execution_result (*deinitialize_ie)(struct ie_object* ie);

    void* handle;
    void* data;
};

struct plugin* plugin_open(const char* filename);
void plugin_close(struct plugin* plugin);

enum plugin_initialization_result plugin_init(struct plugin* plugin, uint32_t allowed_capabilities, const char** error_message);
void plugin_deinit(struct plugin* plugin);

enum plugin_execution_result plugin_execute(struct plugin* plugin, struct ie_object* ie, int32_t* pointer_x, int32_t* pointer_y, uint32_t tick);
enum plugin_execution_result plugin_execute_ie_move(struct plugin* plugin, struct ie_object* ie, int32_t x, int32_t y);
enum plugin_execution_result plugin_execute_ie_attach_mascot(struct plugin* plugin, struct ie_object* ie, struct mascot* mascot);
enum plugin_execution_result plugin_execute_ie_detach_mascot(struct plugin* plugin, struct ie_object* ie, struct mascot* mascot);
enum plugin_execution_result plugin_execute_throw_ie(struct plugin* plugin, struct ie_object* ie, float x_velocity, float y_velocity, float gravity, uint32_t start_tick);
enum plugin_execution_result plugin_execute_stop_ie(struct plugin* plugin, struct ie_object* ie);
enum plugin_execution_result plugin_execute_ie_throw_policy(struct plugin* plugin, int policy);

enum plugin_execution_result plugin_change_capabilities(struct plugin* plugin, uint32_t capabilities);

enum plugin_execution_result plugin_execute_deactivate_ie(struct plugin* plugin, struct ie_object* ie);
enum plugin_execution_result plugin_execute_restore_ies(struct plugin* plugin);

enum plugin_execution_result plugin_get_ie_for_environment(struct plugin* plugin, environment_t* env, struct ie_object** ie);
enum plugin_execution_result plugin_free_ie(struct plugin* plugin, struct ie_object* ie);


#endif
