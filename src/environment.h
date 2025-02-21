/*
    environment.h - wl_shimeji's environment handling

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

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "master_header.h"

#define ENV_DISABLE_TABLETS 1
#define ENV_DISABLE_FRACTIONAL_SCALE 2
#define ENV_DISABLE_VIEWPORTER 4
#define ENV_DISABLE_CURSOR_SHAPE 8

typedef struct environment environment_t;
typedef struct environment_subsurface environment_subsurface_t;
typedef struct environment_pointer environment_pointer_t; // Any pointers, including tablets
typedef struct environment_buffer_factory environment_buffer_factory_t;
typedef struct environment_buffer environment_buffer_t;

enum environment_border_type {
    environment_border_type_none,
    environment_border_type_floor,
    environment_border_type_ceiling,
    environment_border_type_wall,
    environment_border_type_any,
    environment_border_type_invalid
};

enum environment_move_result {
    environment_move_ok,
    environment_move_clamped,
    environment_move_environment_changed,
    environment_move_out_of_bounds,
    environment_move_invalid
};

struct environment_callbacks {
    const struct wl_pointer_listener* pointer_listener;
    const struct wl_surface_listener* surface_listener;
    const struct wl_callback_listener* callback_listener;
    const struct wl_keyboard_listener* keyboard_listener;
    const struct wl_touch_listener* touch_listener;
    const struct zwp_tablet_pad_v2_listener* tablet_pad_listener;
    const struct zwp_tablet_tool_v2_listener* tablet_tool_listener;
    const struct zwp_tablet_v2_listener* tablet_listener;

    void* data;
};

enum environment_init_status {
    ENV_INIT_OK,
    ENV_INIT_ERROR_DISPLAY,
    ENV_INIT_ERROR_GLOBALS,
    ENV_INIT_ERROR_GENERIC,
    ENV_INIT_ERROR_OOM,
    ENV_NOT_INITIALIZED = -1
};

#include "mascot.h"
#include "plugins.h"

enum environment_init_status environment_init(int flags,
    void(*new_listener)(environment_t*), void(*rem_listener)(environment_t*),
    void(*orphaned_mascot)(struct mascot*), void(*mascot_dropped_oob_listener)(struct mascot*, int32_t, int32_t)
);

int environment_dispatch();
void environment_new_env_listener(void(*listener)(environment_t*));
void environment_rem_env_listener(void(*listener)(environment_t*));

const char* environment_get_error();

int environment_get_display_fd();

void environment_unlink(environment_t* env);

enum environment_border_type environment_get_border_type(environment_t* env, int32_t x, int32_t y);

environment_subsurface_t* environment_create_subsurface(environment_t* env);
void environment_destroy_subsurface(environment_subsurface_t* surface);

void environment_subsurface_drag(environment_subsurface_t* surface, environment_pointer_t* pointer);
void environment_subsurface_release(environment_subsurface_t* surface);
void environment_subsurface_unmap(environment_subsurface_t* surface);
void environment_subsurface_reorder(environment_subsurface_t* surface, environment_subsurface_t* sibling, bool above);

void environment_pointer_update_delta(environment_subsurface_t* subsurface, uint32_t tick);

void environment_subsurface_attach(environment_subsurface_t* surface, const struct mascot_pose* pose);
enum environment_move_result environment_subsurface_move(environment_subsurface_t* surface, int32_t dx, int32_t dy, bool use_callback, bool use_interpolation);
enum environment_move_result environment_subsurface_set_position(environment_subsurface_t* surface, int32_t dx, int32_t dy);
environment_t* environment_subsurface_get_environment(environment_subsurface_t* surface);
bool environment_subsurface_move_to_pointer(environment_subsurface_t* surface);
void environment_subsurface_set_offset(environment_subsurface_t* surface, int32_t x, int32_t y);
void environment_subsurface_associate_mascot(environment_subsurface_t* surface, struct mascot* mascot_ptr);
struct mascot* environment_subsurface_get_mascot(environment_subsurface_t* surface);
const struct mascot_pose* environment_subsurface_get_pose(environment_subsurface_t* surface);
bool environment_migrate_subsurface(environment_subsurface_t* surface, environment_t* env);

// Environment info
int32_t environment_screen_width(environment_t* env);
int32_t environment_screen_height(environment_t* env);
int32_t environment_workarea_width(environment_t* env);
int32_t environment_workarea_height(environment_t* env);
int32_t environment_cursor_x(struct mascot* mascot, environment_t* env);
int32_t environment_cursor_y(struct mascot* mascot, environment_t* env);
int32_t environment_cursor_dx(struct mascot* mascot, environment_t* env);
int32_t environment_cursor_dy(struct mascot* mascot, environment_t* env);
int32_t environment_cursor_get_tick_diff(environment_pointer_t* pointer, uint32_t tick);
uint32_t environment_id(environment_t* env);
const char* environment_name(environment_t* env);
const char* environment_desc(environment_t* env);
bool environment_logical_position(environment_t* env, int32_t* lx, int32_t* ly);
bool environment_logical_size(environment_t* env, int32_t* lw, int32_t* lh);

// After click on overlay, callback is calling providing absolute click position, and subsurface it was clicked on (if any)
void environment_select_position(environment_t* env, void (*callback)(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data), void* data);
void environment_set_input_state(environment_t* env, bool active);

float environment_screen_scale(environment_t* env);

bool environment_is_ready(environment_t* env);
bool environment_commit(environment_t* env);
void enviroment_wait_until_ready(environment_t* env);
bool environment_pre_tick(environment_t* env, uint32_t tick);

void environment_set_public_cursor_position(environment_t* env, int32_t x, int32_t y);
void environment_set_ie(environment_t* env, struct ie_object *ie);
struct ie_object* environment_get_ie(environment_t* env);
void environment_get_output_id_info(environment_t* env, const char** name, const char** make, const char** model, const char** desc, uint32_t *id);

// IE's
enum environment_move_result environment_ie_move(environment_t* env, int32_t dx, int32_t dy);
bool environment_ie_allows_move(environment_t* env);
bool environment_ie_throw(environment_t* env, float x_velocity, float y_velocity, float gravity, uint32_t tick);
bool environment_ie_stop_movement(environment_t* env);
bool environment_ie_restore(environment_t* env);

// Misc
void environment_set_broadcast_input_enabled_listener(void(*listener)(bool));
void environment_set_mascot_by_coords_callback(struct mascot* (*callback)(environment_t*, int32_t, int32_t));
void environment_disable_tablet_workarounds(bool value);
const char* environment_get_backend_name();


// Buffers
environment_buffer_factory_t* environment_buffer_factory_new();
void environment_buffer_factory_destroy(environment_buffer_factory_t* factory);
bool environment_buffer_factory_write(environment_buffer_factory_t* factory, const void* data, size_t size);
void environment_buffer_factory_done(environment_buffer_factory_t* factory);
environment_buffer_t* environment_buffer_factory_create_buffer(environment_buffer_factory_t* factory, int32_t width, int32_t height, uint32_t stride, uint32_t offset);

void environment_buffer_add_to_input_region(environment_buffer_t* buffer, int32_t x, int32_t y, int32_t width, int32_t height);
void environment_buffer_subtract_from_input_region(environment_buffer_t* buffer, int32_t x, int32_t y, int32_t width, int32_t height);
void environment_buffer_destroy(environment_buffer_t* buffer);

void environment_set_user_data(environment_t* env, void* data);
void* environment_get_user_data(environment_t* env);

// Interpolation
uint64_t environment_interpolate(environment_t* env);
void environment_subsurface_reset_interpolation(environment_subsurface_t* subsurface); // Resets to current position

#endif
