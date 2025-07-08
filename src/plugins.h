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

typedef struct plugin plugin_t;

// Callbacks
typedef void (*set_cursor_pos_func)(int32_t, int32_t);
typedef void (*set_active_ie_func)(bool, int32_t, int32_t, int32_t, int32_t);
typedef void (*window_moved_hint_func)(void);

typedef int (*init_func)(plugin_t*, set_cursor_pos_func, set_active_ie_func, window_moved_hint_func);
typedef int (*tick_func)(plugin_t*);
typedef int (*move_func)(plugin_t*, int, int);
typedef int (*restore_func)(plugin_t*);
typedef void (*deinit_func)(plugin_t*);

#ifndef BUILD_PLUGIN_SUPPORT
int plugins_init(const char* plugins_search_path, set_cursor_pos_func cursor_cb, set_active_ie_func active_ie_cb, window_moved_hint_func window_moved_hint_cb); // Loads all plugins from the specified path, initializes them using provided callbacks
int plugins_tick(); // Executes all plugins
int plugins_deinit(); // Deinitializes all plugins
int plugins_move_ie(int x, int y);
int plugins_restore_ies();
#endif

int plugin_init(plugin_t* self, const char* name, const char* version, const char* author, const char* description, int64_t target_version);
int plugin_deinit(plugin_t* self);

#endif /* PLUGINS_H */
