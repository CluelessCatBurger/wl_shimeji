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

#include "master_header.h"
#include "physics.h"

typedef struct plugin_output plugin_output_t;
typedef struct plugin_window plugin_window_t;
typedef struct plugin plugin_t;

struct plugin_output_event {
    void (*cursor)(void* userdata, plugin_output_t* output, int32_t x, int32_t y);
    void (*window)(void* userdata, plugin_output_t* output, plugin_window_t* window);
};

enum plugin_window_control_state {
    PLUGIN_WINDOW_CONTROL_STATE_UNKNOWN,
    PLUGIN_WINDOW_CONTROL_STATE_SYSTEM,
    PLUGIN_WINDOW_CONTROL_STATE_MASCOT,
    PLUGIN_WINDOW_CONTROL_STATE_UNAVAILABLE,
};

enum plugin_exec_error {
    PLUGIN_EXEC_OK,
    PLUGIN_EXEC_ENOENT,
    PLUGIN_EXEC_SEGV,
    PLUGIN_EXEC_EINVAL,
    PLUGIN_EXEC_ABORT
};

struct plugin_window_event {
    void (*geometry)(void* userdata, plugin_window_t* window, struct bounding_box geometry);
    void (*ordering)(void* userdata, plugin_window_t* window, int32_t order_index);
    void (*control)(void* userdata, plugin_window_t* window, enum plugin_window_control_state state);
    void (*destroy)(void* userdata, plugin_window_t* window);
};

plugin_t* plugin_load(int32_t at_fd, const char* path, enum plugin_exec_error* error);
plugin_t* plugin_ref(plugin_t* plugin);
void plugin_unref(plugin_t* plugin);
void plugin_unload(plugin_t* plugin);
int32_t plugin_get_caps(plugin_t* plugin, enum plugin_exec_error* error);
enum plugin_exec_error* plugin_set_caps(plugin_t* plugin, int32_t caps);
enum plugin_exec_error* plugin_tick(plugin_t* plugin);

plugin_output_t* plugins_request_output(struct bounding_box* geometry);
plugin_output_t* plugins_output_ref(plugin_output_t* output);
void plugins_output_unref(plugin_output_t* output);
void plugin_output_set_listener(plugin_output_t* output, struct plugin_output_event* listener, void* userdata);
void plugin_output_commit(plugin_output_t* output);
void plugin_output_destroy(plugin_output_t* output); // Calls plugin_output_unref(output) internally

plugin_window_t* plugin_window_ref(plugin_window_t* window);
void plugin_window_unref(plugin_window_t* window);
void plugin_window_set_listener(plugin_window_t* window, struct plugin_window_event* listener, void* userdata);
bool plugin_window_grab_control(plugin_window_t* window);
bool plugin_window_move(plugin_window_t* window, int32_t x, int32_t y);
bool plugin_window_drop_control(plugin_window_t* window);
bool plugin_window_restore_position(plugin_window_t* window);
struct bounding_box* plugin_window_geometry(plugin_window_t* window);


#endif
