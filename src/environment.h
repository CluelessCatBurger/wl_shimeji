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
#include "physics.h"

#define ENV_DISABLE_TABLETS 1
#define ENV_DISABLE_FRACTIONAL_SCALE 2
#define ENV_DISABLE_VIEWPORTER 4
#define ENV_DISABLE_CURSOR_SHAPE 8

typedef struct environment environment_t;
typedef struct environment_subsurface environment_subsurface_t;
typedef struct environment_pointer environment_pointer_t; // Any pointers, including tablets
typedef struct environment_buffer_factory environment_buffer_factory_t;
typedef struct environment_shm_pool environment_shm_pool_t;
typedef struct environment_buffer environment_buffer_t;
typedef struct environment_popup environment_popup_t;

struct environment_popup_listener {
    void (*mapped)(void* data, environment_popup_t* popup);
    void (*unmapped)(void* data, environment_popup_t* popup);
    void (*dismissed)(void* data, environment_popup_t* popup);
    void (*enter)(void* data, environment_popup_t* popup, uint32_t serial, uint32_t x, uint32_t y);
    void (*leave)(void* data, environment_popup_t* popup);
    void (*motion)(void* data, environment_popup_t* popup, uint32_t x, uint32_t y);
    void (*clicked)(void* data, environment_popup_t* popup, uint32_t serial, uint32_t x, uint32_t y);
};

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


enum environment_init_status {
    ENV_INIT_OK,
    ENV_INIT_ERROR_DISPLAY,
    ENV_INIT_ERROR_GLOBALS,
    ENV_INIT_ERROR_GENERIC,
    ENV_INIT_ERROR_OOM,
    ENV_NOT_INITIALIZED = -1
};

#include "mascot.h"
#include "mascot_config_parser.h"

enum environment_init_status environment_init(int flags,
    void(*new_listener)(environment_t*), void(*rem_listener)(environment_t*),
    void(*orphaned_mascot)(struct mascot*)
);

int environment_dispatch();
void environment_new_env_listener(void(*listener)(environment_t*));
void environment_rem_env_listener(void(*listener)(environment_t*));

const char* environment_get_error();

int environment_get_display_fd();

void environment_unlink(environment_t* env);

enum environment_border_type environment_get_border_type(environment_t* env, int32_t x, int32_t y);
enum environment_border_type environment_get_border_type_rect(environment_t* env, int32_t x, int32_t y, struct bounding_box* rect, int32_t mask);

environment_subsurface_t* environment_create_subsurface(environment_t* env);
void environment_destroy_subsurface(environment_subsurface_t* surface);

void environment_subsurface_drag(environment_subsurface_t* surface, environment_pointer_t* pointer);
void environment_subsurface_release(environment_subsurface_t* surface);
void environment_subsurface_unmap(environment_subsurface_t* surface);
void environment_subsurface_reorder(environment_subsurface_t* surface, environment_subsurface_t* sibling, bool above);
void environment_subsurface_scale_coordinates(environment_subsurface_t* surface, int32_t* x, int32_t* y);

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
int32_t environment_workarea_left(environment_t* env);
int32_t environment_workarea_right(environment_t* env);
int32_t environment_workarea_top(environment_t* env);
int32_t environment_workarea_bottom(environment_t* env);
int32_t environment_workarea_width(environment_t* env);
int32_t environment_workarea_height(environment_t* env);

int32_t environment_workarea_coordinate_aligned(environment_t* env, int32_t border_type, int32_t alignment_type);
int32_t environment_workarea_width_aligned(environment_t* env, int32_t alignment_type);
int32_t environment_workarea_height_aligned(environment_t* env, int32_t alignment_type);

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
void environment_get_output_id_info(environment_t* env, const char** name, const char** make, const char** model, const char** desc, uint32_t *id);

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
void environment_buffer_scale_input_region(environment_buffer_t* buffer, float scale_factor);
void environment_buffer_destroy(environment_buffer_t* buffer);

void environment_set_user_data(environment_t* env, void* data);
void* environment_get_user_data(environment_t* env);

// Interpolation
uint64_t environment_interpolate(environment_t* env);
void environment_subsurface_reset_interpolation(environment_subsurface_t* subsurface); // Resets to current position

// Mascots
void environment_summon_mascot(
    environment_t* environment, struct mascot_prototype* prototype,
    int32_t x, int32_t y, void(*callback)(struct mascot*, void*), void* data
);
void environment_remove_mascot(environment_t* environment, struct mascot* mascot);
void environment_set_prototype_store(environment_t* environment, mascot_prototype_store* store);
uint32_t environment_tick(environment_t* environment, uint32_t tick);
uint32_t environment_mascot_count(environment_t* environment);

void environment_set_global_coordinates_searcher(
    environment_t* (environment_by_global_coordinates)(int32_t x, int32_t y)
);

void environment_global_coordinates_delta(
    environment_t* environment_a,
    environment_t* environment_b,
    int32_t* dx, int32_t* dy
);

void environment_to_global_coordinates(
    environment_t* environment,
    int32_t* x, int32_t* y
);

void environment_set_affordance_manager(environment_t* environment, struct mascot_affordance_manager* manager);

struct bounding_box* environment_local_geometry(environment_t* environment);
struct bounding_box* environment_global_geometry(environment_t* environment);
struct mascot* environment_mascot_by_coordinates(environment_t* environment, int32_t x, int32_t y);
struct mascot* environment_mascot_by_id(environment_t* environment, uint32_t id);
struct list* environment_mascot_list(environment_t* environment, pthread_mutex_t** list_mutex);

// Unified outputs
void environment_announce_neighbor(environment_t* environment, environment_t* neighbor);
void environment_widthdraw_neighbor(environment_t* environment, environment_t* neighbor);
bool environment_neighbor_border(environment_t* environment, int32_t x, int32_t y);

bool environment_ask_close(environment_t* environment);

environment_shm_pool_t* environment_import_shm_pool(int32_t fd, uint32_t size);
environment_buffer_t* environment_shm_pool_create_buffer(
    environment_shm_pool_t* pool,
    uint32_t offset,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    uint32_t format
);
void environment_shm_pool_destroy(environment_shm_pool_t* pool);

environment_popup_t* environment_popup_create(environment_t* environment, struct mascot* mascot, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void environment_popup_attach(environment_popup_t* popup, environment_buffer_t* buffer);
void environment_popup_commit(environment_popup_t* popup);
void environment_popup_dismiss(environment_popup_t* popup);
void environment_popup_add_listener(environment_popup_t* popup, struct environment_popup_listener listener, void* data);

// IEs

void environment_set_active_ie(bool is_active, struct bounding_box geometry);
bool environment_ie_is_active();
struct bounding_box environment_get_active_ie(environment_t* environment);
void environment_recalculate_ie_attachement(environment_t* env, bool is_active, struct bounding_box geometry);

#endif
