/*
    environment-wayland.h - wl_shimeji's environment handling, wayland backend

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

#define _GNU_SOURCE

#include "wayland_includes.h"
#include <sys/mman.h>
#include "mascot_atlas.h"
#include "mascot.h"
#include "layer_surface.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include "environment.h"
#include <linux/input-event-codes.h>
#include <wayland-util.h>
#include <errno.h>
#include "plugins.h"
#include "config.h"
#include "list.h"
#include <wayland-cursor.h>

// Workarounds section (SMH my head hyprland)
bool we_on_hyprland = false;
bool we_on_kde = false;

// Disable workarounds
bool disable_tablet_workarounds = false;

bool why_tablet_v2_proximity_in_events_received_by_parent_question_mark = false;

struct wl_display* display = NULL;
struct wl_registry* registry = NULL;
struct wl_compositor* compositor = NULL;
struct wl_shm* shm_manager = NULL;
struct wl_subcompositor* subcompositor = NULL;
struct wl_seat* seat = NULL;

struct wl_cursor_theme* cursor_theme = NULL;

// wlr extensions
struct zwlr_layer_shell_v1* wlr_layer_shell = NULL;

// stable extensions
struct xdg_wm_base* xdg_wm_base = NULL;
struct zwp_tablet_manager_v2* tablet_manager = NULL;
struct zwp_tablet_seat_v2* tablet_seat = NULL;
struct wp_viewporter* viewporter = NULL;

// staging extensions
struct wp_fractional_scale_manager_v1* fractional_manager = NULL;
struct wp_cursor_shape_manager_v1* cursor_shape_manager = NULL;

// unstable extensions
struct zxdg_output_manager_v1* xdg_output_manager = NULL;

void (*new_environment)(environment_t*) = NULL;
void (*rem_environment)(environment_t*) = NULL;
void (*orphaned_mascot)(struct mascot*) = NULL;
void (*mascot_dropped_oob)(struct mascot*, int32_t x, int32_t y) = NULL;
void (*env_broadcast_input_enabled)(bool) = NULL;
struct mascot* (*mascot_by_coordinates)(environment_t*, int32_t, int32_t) = NULL;

struct wl_buffer* anchor_buffer = NULL;
struct wl_region* empty_region = NULL;

int32_t active_grabbers_count = 0;

#define CURRENT_DEVICE_TYPE_MOUSE 0
#define CURRENT_DEVICE_TYPE_PEN 1
#define CURRENT_DEVICE_TYPE_ERASER 2
#define CURRENT_DEVICE_TYPE_TOUCH 3

#define BUTTON_TYPE_LEFT 0
#define BUTTON_TYPE_MIDDLE 1
#define BUTTON_TYPE_RIGHT 2
#define BUTTON_TYPE_PEN 3

struct output_info {
    struct wl_output* output;

    const char* name;
    const char* make;
    const char* model;
    const char* desc;
    uint32_t id;
    uint32_t width;
    uint32_t height;
    int32_t scale;
    uint32_t refresh;
    uint32_t transform;
    uint32_t subpixel;
    uint32_t x,y;
    uint32_t pwidth;
    uint32_t pheight;
};

struct environment {
    uint32_t id;
    struct output_info output;
    float scale;
    uint32_t width, height;
    struct layer_surface* root_surface;
    bool is_ready;
    environment_subsurface_t* root_environment_subsurface;
    bool pending_commit;
    int32_t interactive_window_count;

    // unstable extensions
    struct zxdg_output_v1* xdg_output;
    const char* xdg_output_name;
    const char* xdg_output_desc;
    bool xdg_output_done;
    int32_t lwidth, lheight;
    int32_t lx, ly;

    struct list* referenced_mascots;
    struct ie_object* ie;

    bool select_active;
    void (*select_callback)(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data);
    void* select_data;
};

struct wl_surface_data {
    enum {
        WL_SURFACE_ROLE_SUBSURFACE,
        WL_SURFACE_ROLE_LAYER_COMPONENT,
        WL_SURFACE_ROLE_PARENT_SURFACE
    } role;
    union {
        environment_subsurface_t* subsurface;
        struct layer_surface* layer_surface;
    } role_data;
    void* callbacks;
    environment_t* associated_environment;
};

struct environment_subsurface {
    struct wl_surface* surface;
    struct wl_subsurface* subsurface;
    struct wp_viewport* viewport;
    struct wp_fractional_scale_v1* fractional_scale;
    const struct mascot_pose* pose;
    environment_t* env;
    struct mascot* mascot;
    bool is_grabbed;
    struct environment_pointer* drag_pointer;
    int32_t x, y;
    int32_t width, height;
    int32_t offset_x, offset_y;
};

#define EVENT_FRAME_BUTTONS 0x01
#define EVENT_FRAME_SURFACE 0x02
#define EVENT_FRAME_MOTIONS 0x04
#define EVENT_FRAME_AXIS    0x08
#define EVENT_FRAME_PROXIMITY 0x10

// Primary button, usually left mouse button, on tablets and touchscreens corresponds to the down event
#define EVENT_FRAME_PRIMARY_BUTTON POINTER_PRIMARY_BUTTON
#define EVENT_FRAME_SECONDARY_BUTTON POINTER_SECONDARY_BUTTON
#define EVENT_FRAME_THIRD_BUTTON 0x04
// Button id passed with mask 0xFFFFFFF0, shifted right by 0xF (unused)
#define EVENT_FRAME_MISC_BUTTON POINTER_THIRD_BUTTON

struct environment_event_frame {
    uint32_t mask;

    // Buttons
    uint32_t buttons_pressed;
    uint32_t buttons_released;

    // Surface
    struct wl_surface* surface_changed;
    uint32_t enter_serial;

    // Motions
    wl_fixed_t surface_local_x;
    wl_fixed_t surface_local_y;
};

struct environment_pointer {
    // Position of the pointer, in surface-local coordinates
    int32_t x, y;
    int32_t dx, dy;

    int32_t surface_x, surface_y;

    // Public position of the pointer, in global coordinates, exposed to mascots, only by plugins
    int32_t public_x, public_y;

    uint32_t reference_tick;

    environment_subsurface_t* grabbed_subsurface;
    enum {
        ENVIRONMENT_POINTER_PROTOCOL_POINTER,
        ENVIRONMENT_POINTER_PROTOCOL_TABLET,
        ENVIRONMENT_POINTER_PROTOCOL_TOUCH
    } protocol;

    // Corresponding device
    union {
        struct wl_pointer* pointer;
        struct zwp_tablet_tool_v2* tablet;
        struct wl_touch* touch;
    } device;

    // Device aux data
    void* aux_data;

    uint32_t enter_serial;
    uint32_t buttons_state;

    environment_t* above_environment;

    struct wl_surface* above_surface;

    // Cursor shapes
    enum wp_cursor_shape_device_v1_shape current_shape;
    struct wp_cursor_shape_device_v1* cursor_shape_device;
    struct wl_cursor* cursor;

    // Staging changes
    struct environment_event_frame frame;

};

struct environment_buffer_factory {
    struct wl_shm_pool* pool;
    uint64_t size;
    int memfd;
    bool done;
};

struct environment_buffer {
    struct wl_buffer* buffer;
    struct wl_region* region;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

// Pointer with most recent activity
environment_pointer_t default_active_pointer = {0};
environment_pointer_t* active_pointer = &default_active_pointer;

#define ACTIVATE_POINTER(pointer) active_pointer = pointer

static uint32_t yconv(environment_t* env, uint32_t y);

// Helper structs -----------------------------------------------------------

struct envs_queue {
    int32_t flags;
    uint8_t envs_count;
    environment_t* envs[16];
    bool post_init;
};

// Helper functions ---------------------------------------------------------

environment_t* environment_from_surface(struct wl_surface* surface)
{
    if (!surface) return NULL;
    struct wl_surface_data* data = wl_surface_get_user_data(surface);
    if (!data) return NULL;
    return data->associated_environment;
}

environment_subsurface_t* environment_subsurface_from_surface(struct wl_surface* surface)
{
    if (!surface) return NULL;
    struct wl_surface_data* data = wl_surface_get_user_data(surface);
    if (!data) return NULL;
    if (data->role != WL_SURFACE_ROLE_SUBSURFACE) return NULL;
    return data->role_data.subsurface;
}

bool is_root_surface(struct wl_surface* surface)
{
    if (!surface) return NULL;
    struct wl_surface_data* data = wl_surface_get_user_data(surface);
    if (!data) return false;
    return data->role == WL_SURFACE_ROLE_PARENT_SURFACE;
}

struct layer_surface* layer_surface_from_surface(struct wl_surface* surface)
{
    if (!surface) return NULL;
    struct wl_surface_data* data = wl_surface_get_user_data(surface);
    if (!data) return NULL;
    if (data->role != WL_SURFACE_ROLE_LAYER_COMPONENT && data->role != WL_SURFACE_ROLE_PARENT_SURFACE) return NULL;
    return data->role_data.layer_surface;
}

struct wl_surface_data* wl_surface_set_data(struct wl_surface* surface, uint8_t role, void* data, environment_t* env)
{
    if (!surface) return NULL;
    struct wl_surface_data* surface_data = wl_surface_get_user_data(surface);
    if (!surface_data) {
        surface_data = calloc(1, sizeof(struct wl_surface_data));
        if (!surface_data) return NULL;
        wl_surface_set_user_data(surface, surface_data);
    }
    surface_data->role = role;
    if (role == WL_SURFACE_ROLE_SUBSURFACE) {
        surface_data->role_data.subsurface = data;
    } else if (role == WL_SURFACE_ROLE_LAYER_COMPONENT || role == WL_SURFACE_ROLE_PARENT_SURFACE) {
        surface_data->role_data.layer_surface = data;
    }
    surface_data->associated_environment = env;
    return surface_data;
}

void wl_surface_clear_data(struct wl_surface* surface)
{
    if (!surface) return;
    struct wl_surface_data* surface_data = wl_surface_get_user_data(surface);
    if (!surface_data) return;
    free(surface_data);
    wl_surface_set_user_data(surface, NULL);
}

void wl_surface_attach_callbacks(struct wl_surface* surface, void* callbacks)
{
    if (!surface) return;
    struct wl_surface_data* surface_data = wl_surface_get_user_data(surface);
    if (!surface_data) return;
    surface_data->callbacks = callbacks;
}

void* wl_surface_get_callbacks(struct wl_surface* surface)
{
    if (!surface) return NULL;
    struct wl_surface_data* surface_data = wl_surface_get_user_data(surface);
    if (!surface_data) return NULL;
    return surface_data->callbacks;
}

enum environment_border_type environment_try_ie_collision(environment_t *env, int32_t from_x, int32_t from_y, int32_t to_x, int32_t to_y, int32_t *out_x, int32_t *out_y)
{
    // This function is used to check if the line from (from_x, from_y) to (to_x, to_y) intersects with the interactive window borders
    // If it does, it returns the border type that was intersected
    // If it doesn't, it returns environment_border_type_invalid

    if (!env->is_ready) return environment_border_type_invalid;
    if (!env->ie) return environment_border_type_invalid;
    if (!env->ie->active) return environment_border_type_invalid;

    // Handle the case where the line is a point (from_x == to_x && from_y == to_y)
    if (from_x == to_x && from_y == to_y) {
        // Check if the point is on the "floor" (top border, treated as floor)
        if (from_y == env->ie->y && from_x >= env->ie->x && from_x <= env->ie->x + env->ie->width) {
            *out_x = from_x;
            *out_y = from_y;
            return environment_border_type_floor;  // Treat top as floor
        }

        // Check if the point is on the "ceiling" (bottom border, treated as ceiling)
        if (from_y == env->ie->y + env->ie->height && from_x >= env->ie->x && from_x <= env->ie->x + env->ie->width) {
            *out_x = from_x;
            *out_y = from_y;
            return environment_border_type_ceiling;  // Treat bottom as ceiling
        }

        // Check if the point is on the left border
        if (from_x == env->ie->x && from_y >= env->ie->y && from_y <= env->ie->y + env->ie->height) {
            *out_x = from_x;
            *out_y = from_y;
            return environment_border_type_wall;
        }

        // Check if the point is on the right border
        if (from_x == env->ie->x + env->ie->width && from_y >= env->ie->y && from_y <= env->ie->y + env->ie->height) {
            *out_x = from_x;
            *out_y = from_y;
            return environment_border_type_wall;
        }

        // If no border is hit, return invalid
        return environment_border_type_invalid;
    }

    enum environment_border_type result = environment_border_type_invalid;

    // Check for floor (top border, treated as floor): Movement from above to below (falling onto the top border)
    if (from_y < to_y && from_y < env->ie->y && to_y >= env->ie->y) {
        // The line is moving downwards (falling onto the top of the box)
        int32_t x = from_x + (env->ie->y - from_y) * (to_x - from_x) / (to_y - from_y);
        if (x >= env->ie->x && x <= env->ie->x + env->ie->width) {
            *out_x = x;
            *out_y = env->ie->y;
            result = environment_border_type_floor;  // Treat top as floor
        }
    }

    // // Check for ceiling (bottom border, treated as ceiling): Movement from below to above (moving upward to hit the bottom)
    // else if (from_y > to_y && from_y > env->ie->y + env->ie->height && to_y <= env->ie->y + env->ie->height) {
    //     // The line is moving upwards (hitting the bottom of the box)
    //     int32_t x = from_x + (env->ie->y + env->ie->height - from_y) * (to_x - from_x) / (to_y - from_y);
    //     if (x >= env->ie->x && x <= env->ie->x + env->ie->width) {
    //         *out_x = x;
    //         *out_y = env->ie->y + env->ie->height;
    //         result = environment_border_type_ceiling;  // Treat bottom as ceiling
    //     }
    // }

    // Check for left side (wall) collision: Movement from left to right, and the line intersects with the left border
    else if (from_x < env->ie->x && to_x > env->ie->x) {
        int32_t y = from_y + (env->ie->x - from_x) * (to_y - from_y) / (to_x - from_x);
        if (y >= env->ie->y && y <= env->ie->y + env->ie->height) {
            *out_x = env->ie->x;
            *out_y = y;
            result = environment_border_type_wall;
        }
    }

    // Check for right side (wall) collision: Movement from right to left, and the line intersects with the right border
    else if (from_x > env->ie->x + env->ie->width && to_x < env->ie->x + env->ie->width) {
        int32_t y = from_y + (env->ie->x + env->ie->width - from_x) * (to_y - from_y) / (to_x - from_x);
        if (y >= env->ie->y && y <= env->ie->y + env->ie->height) {
            *out_x = env->ie->x + env->ie->width;
            *out_y = y;
            result = environment_border_type_wall;
        }
    }

    // Check for movement entirely along the floor (top border)
    else if (from_y == env->ie->y && to_y == env->ie->y &&
        (from_x <= env->ie->x + env->ie->width && to_x >= env->ie->x)) {
        *out_x = from_x;  // or a midpoint if you need specific coordinates
        *out_y = env->ie->y;
        result = environment_border_type_floor;
    }

    // Check for movement entirely along the ceiling (bottom border)
    else if (from_y == env->ie->y + env->ie->height && to_y == env->ie->y + env->ie->height &&
        (from_x <= env->ie->x + env->ie->width && to_x >= env->ie->x)) {
        *out_x = from_x;  // or a midpoint if you need specific coordinates
        *out_y = env->ie->y + env->ie->height;
        result = environment_border_type_ceiling;
    }

    // Check for movement entirely along the left wall
    else if (from_x == env->ie->x && to_x == env->ie->x &&
        (from_y <= env->ie->y + env->ie->height && to_y >= env->ie->y)) {
        *out_x = env->ie->x;
        *out_y = from_y;  // or a midpoint if you need specific coordinates
        result = environment_border_type_wall;
    }

    // Check for movement entirely along the right wall
    else if (from_x == env->ie->x + env->ie->width && to_x == env->ie->x + env->ie->width &&
        (from_y <= env->ie->y + env->ie->height && to_y >= env->ie->y)) {
        *out_x = env->ie->x + env->ie->width;
        *out_y = from_y;  // or a midpoint if you need specific coordinates
        result = environment_border_type_wall;
    }
    return result;
}

uint32_t yconv(environment_t* env, uint32_t y)
{
    return (int32_t)environment_workarea_height(env) - y;
}

void enable_input_on_environments(bool enable)
{
    if (enable) {
        // Get and increment atomically active grabbers
        int32_t oldval = __atomic_fetch_add(&active_grabbers_count, 1, __ATOMIC_SEQ_CST);
        if (!oldval) {
            if (env_broadcast_input_enabled) {
                env_broadcast_input_enabled(true);
            }
        }
    } else {
        // Get and decrement atomically active grabbers
        int32_t oldval = __atomic_fetch_sub(&active_grabbers_count, 1, __ATOMIC_SEQ_CST);
        if (oldval) {
            if (env_broadcast_input_enabled) {
                env_broadcast_input_enabled(false);
            }
        }
    }
}

#ifndef PLUGINSUPPORT_IMPLEMENTATION

bool environment_pointer_apply_cursor(environment_pointer_t* pointer, int32_t cursor_type)
{
    if (!pointer) return false;
    if (!pointer->cursor_shape_device && !cursor_theme) return false;
    struct wl_cursor* cursor = pointer->cursor;
    struct wl_cursor_image* image = NULL;
    int32_t cursor_shape = pointer->current_shape;

    if (cursor_type != -1) {
        switch (cursor_type) {
            case mascot_hotspot_cursor_pointer:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
                }
                break;

            case mascot_hotspot_cursor_hand:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "hand1");
                }
                break;

            case mascot_hotspot_cursor_crosshair:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "crosshair");
                }
                break;

            case mascot_hotspot_cursor_text:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "text");
                }
                break;

            case mascot_hotspot_cursor_move:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "move");
                }
                break;

            case mascot_hotspot_cursor_wait:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "watch");
                }
                break;

            case mascot_hotspot_cursor_help:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "question_arrow");
                }
                break;

            case mascot_hotspot_cursor_progress:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "progress");
                }
                break;

            case mascot_hotspot_cursor_deny:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "circle");
                }
                break;

            default:
                cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
                if (cursor_theme) {
                    cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
                }
                break;
        }
    }

    if (pointer->cursor_shape_device) {
        wp_cursor_shape_device_v1_set_shape(pointer->cursor_shape_device, pointer->enter_serial, cursor_shape);
        pointer->current_shape = cursor_shape;
        return true;
    }

    if (!cursor) {
        return false;
    }

    image = cursor->images[0];

    if (pointer->protocol == ENVIRONMENT_POINTER_PROTOCOL_POINTER) {
        wl_pointer_set_cursor(pointer->device.pointer, pointer->enter_serial, pointer->above_surface, image->hotspot_x, image->hotspot_y);
    } else if (pointer->protocol == ENVIRONMENT_POINTER_PROTOCOL_TABLET) {
        zwp_tablet_tool_v2_set_cursor(pointer->device.tablet, pointer->enter_serial, pointer->above_surface, image->hotspot_x, image->hotspot_y);
    }
    pointer->cursor = cursor;
    return true;

}

environment_pointer_t* allocate_env_pointer(int32_t protocol, void* proxy_object)
{
    environment_pointer_t* pointer = (environment_pointer_t*)calloc(1, sizeof(environment_pointer_t));
    if (!pointer) ERROR("Failed to allocate memory for environment pointer");

    pointer->protocol = protocol;

    if (protocol == ENVIRONMENT_POINTER_PROTOCOL_POINTER) {
        pointer->device.pointer = (struct wl_pointer*)proxy_object;
        if (cursor_shape_manager) {
            pointer->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(cursor_shape_manager, (struct wl_pointer*)proxy_object);
            pointer->current_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        }
    } else if (protocol == ENVIRONMENT_POINTER_PROTOCOL_TABLET) {
        pointer->device.tablet = (struct zwp_tablet_tool_v2*)proxy_object;
        if (cursor_shape_manager) {
            pointer->cursor_shape_device = wp_cursor_shape_manager_v1_get_tablet_tool_v2(cursor_shape_manager, (struct zwp_tablet_tool_v2*)proxy_object);
            pointer->current_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        }
    } else if (protocol == ENVIRONMENT_POINTER_PROTOCOL_TOUCH) {
        pointer->device.touch = (struct wl_touch*)proxy_object;
    }

    if (cursor_theme) {
        pointer->cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
    }

    return pointer;
}

void free_env_pointer(environment_pointer_t* pointer)
{
    if (!pointer) return;

    if (active_pointer == pointer) {
        active_pointer = &default_active_pointer;
        active_pointer->x = pointer->x;
        active_pointer->y = pointer->y;
        active_pointer->public_x = pointer->public_x;
        active_pointer->public_y = pointer->public_y;
        active_pointer->dx = pointer->dx;
        active_pointer->dy = pointer->dy;
        active_pointer->surface_x = pointer->surface_x;
        active_pointer->surface_y = pointer->surface_y;
    }

    if (pointer->cursor_shape_device) {
        wp_cursor_shape_device_v1_destroy(pointer->cursor_shape_device);
    }
    free(pointer);
}

static void block_until_synced();
static char error_m[1024] = {0};
static uint16_t error_offt = 0;
static enum environment_init_status init_status = ENV_NOT_INITIALIZED;
static uint32_t display_id = 0;
static uint32_t wl_refcounter = 0;

// -----------------------------------------

static void on_sync_callback(void* data, struct wl_callback* callback, uint32_t time)
{
    bool* lock = (bool*)data;
    wl_callback_destroy(callback);
    *lock = false;
    UNUSED(time);
}

struct wl_callback_listener sync_listener = {
    .done = on_sync_callback
};

// Helper functions ---------------------------------------------------------

static void block_until_synced()
{
    bool lock = false;
    struct wl_callback* callback = wl_display_sync(display);
    wl_callback_add_listener(callback, &sync_listener, &lock);
    wl_display_roundtrip(display);
    while(wl_display_dispatch(display) != -1 && lock);
}

static void environment_dimensions_changed_callback(uint32_t width, uint32_t height, void* data)
{
    environment_t* env = (environment_t*)data;
    env->width = width;
    env->height = height;
}

static void environment_wants_to_close_callback(void* data)
{
    environment_t* env = (environment_t*)data;
    INFO("Environment %u wants to close", env->id);
    if (rem_environment) {
        rem_environment(env);
    }
}

static void mascot_on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
static void mascot_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void mascot_on_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void mascot_on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface);
static void mascot_on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);

static const struct wl_pointer_listener mascot_pointer_listener = {
    .enter = mascot_on_pointer_enter,
    .button = mascot_on_pointer_button,
    .frame = NULL,
    .axis = mascot_on_pointer_axis,
    .leave = mascot_on_pointer_leave,
    .motion = mascot_on_pointer_motion,
};

static void environment_on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
static void environment_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void environment_on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void environment_on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface);

static const struct wl_pointer_listener environment_pointer_listener = {
    .enter = environment_on_pointer_enter,
    .button = environment_on_pointer_button,
    .frame = NULL,
    .axis = NULL,
    .leave = environment_on_pointer_leave,
    .motion = environment_on_pointer_motion,
};

static void xdg_output_logical_position(void* data, struct zxdg_output_v1* xdg_output, int32_t x, int32_t y);
static void xdg_output_logical_size(void* data, struct zxdg_output_v1* xdg_output, int32_t width, int32_t height);
static void xdg_output_name(void* data, struct zxdg_output_v1* xdg_output, const char* name);
static void xdg_output_description(void* data, struct zxdg_output_v1* xdg_output, const char* description);
static void xdg_output_done(void* data, struct zxdg_output_v1* xdg_output);

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_logical_position,
    .logical_size = xdg_output_logical_size,
    .name = xdg_output_name,
    .description = xdg_output_description,
    .done = xdg_output_done
};

static void on_preffered_scale(void* data, struct wp_fractional_scale_v1* fractional_scale, uint32_t scale);

static const struct wp_fractional_scale_v1_listener fractional_scale_manager_v1_listener = {
    .preferred_scale = on_preffered_scale,
};

enum environment_init_status dispatch_envs_queue(struct envs_queue* envs)
{
    for (size_t i = 0; i < envs->envs_count; i++) {
        environment_t* env = envs->envs[i];
        env->root_surface = layer_surface_create(env->output.output, config_get_overlay_layer());
        if (!env->root_surface) {
            WARN("FAILED TO CREATE LAYER SURFACE ON OUTPUT %u aka %s", env->id, env->output.name);
            if (rem_environment) {
                rem_environment(env);
            }
            continue;
        }
        layer_surface_set_dimensions_callback(env->root_surface, environment_dimensions_changed_callback, env);
        layer_surface_set_closed_callback(env->root_surface, environment_wants_to_close_callback, env);
        layer_surface_map(env->root_surface);
        block_until_synced();
        wl_surface_set_data(env->root_surface->surface, WL_SURFACE_ROLE_PARENT_SURFACE, (void*)env->root_surface, env);
        if (env->root_surface->configure_serial) {
            env->is_ready = true;
            env->root_environment_subsurface = environment_create_subsurface(env);
            wl_surface_set_data(env->root_environment_subsurface->surface, WL_SURFACE_ROLE_LAYER_COMPONENT, env->root_surface, env);

            env->scale = env->output.scale;

            if (fractional_manager) {
                env->root_environment_subsurface->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(fractional_manager, env->root_environment_subsurface->surface);
                if (!env->root_environment_subsurface->fractional_scale) {
                    wl_surface_clear_data(env->root_environment_subsurface->surface);
                    wl_surface_destroy(env->root_environment_subsurface->surface);
                    free(env->root_environment_subsurface);
                    error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Failed to get fractional scale for root environment subsurface\n");
                    init_status = ENV_INIT_ERROR_GENERIC;
                    return ENV_INIT_ERROR_GENERIC;
                }
                wp_fractional_scale_v1_add_listener(env->root_environment_subsurface->fractional_scale, &fractional_scale_manager_v1_listener, env);
            }

            if (xdg_output_manager) {
                env->xdg_output = zxdg_output_manager_v1_get_xdg_output(xdg_output_manager, env->output.output);
                if (!env->xdg_output) {
                    wl_surface_clear_data(env->root_environment_subsurface->surface);
                    wl_surface_destroy(env->root_environment_subsurface->surface);
                    free(env->root_environment_subsurface);
                    error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Failed to get xdg output for root environment subsurface\n");
                    init_status = ENV_INIT_ERROR_GENERIC;
                    return ENV_INIT_ERROR_GENERIC;
                }
                zxdg_output_v1_add_listener(env->xdg_output, &xdg_output_listener, env);
            }

            struct environment_callbacks* callbacks = (struct environment_callbacks*)calloc(1, sizeof(struct environment_callbacks));
            if (!callbacks) {
                error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Failed to allocate memory for environment callbacks\n");
                init_status = ENV_INIT_ERROR_OOM;
                return init_status;
            }
            callbacks->data = (void*)env->root_environment_subsurface;
            callbacks->pointer_listener = &environment_pointer_listener;

            wl_surface_attach_callbacks(env->root_surface->surface, (void*)callbacks);
            wl_surface_attach(env->root_environment_subsurface->surface, anchor_buffer, 0, 0);
        }
        environment_commit(env);
    }
    return ENV_INIT_OK;
}

// Wayland callbacks ----------------------------------------------------------

static void on_format (void* data, struct wl_shm* shm, uint32_t format)
{
    UNUSED(data);
    UNUSED(shm);
    if (format == WL_SHM_FORMAT_XRGB8888) {
        int mfd = memfd_create("onepixel", O_RDWR);
        if (mfd < 0) {
            ERROR("memfd_create failed: %s", strerror(errno));
            return;
        }
        if (ftruncate(mfd, 4) < 0) {
            ERROR("ftruncate failed: %s", strerror(errno));
            return;
        }
        struct wl_shm_pool* pool = wl_shm_create_pool(shm, mfd, 4);
        struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, 1, 1, 4, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(mfd);
        anchor_buffer = buffer;
    }
}

static const struct wl_shm_listener wl_shm_listener_cb = {
    .format = on_format
};

static void on_output_geometry(void* data, struct wl_output* output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char* make, const char* model, int32_t transform)
{
    UNUSED(output);
    environment_t* env = (environment_t*)data;
    env->output.x = x;
    env->output.y = y;
    env->output.pwidth = physical_width;
    env->output.pheight = physical_height;
    env->output.subpixel = subpixel;
    env->output.make = make;
    env->output.model = model;
    env->output.transform = transform;
}

static void on_output_mode(void* data, struct wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    UNUSED(output);
    UNUSED(flags);
    environment_t* env = (environment_t*)data;
    env->output.width = width;
    env->output.height = height;
    env->output.refresh = refresh;
}

static void on_output_done(void* data, struct wl_output* output)
{
    UNUSED(output);
    UNUSED(data);
}

static void on_output_scale(void* data, struct wl_output* output, int32_t factor)
{
    UNUSED(output);
    environment_t* env = (environment_t*)data;
    env->output.scale = factor;
}

static void on_output_name(void* data, struct wl_output* output, const char* name)
{
    UNUSED(output);
    environment_t* env = (environment_t*)data;
    env->output.name = strdup(name);
}

static void on_output_description(void* data, struct wl_output* output, const char* desc)
{
    UNUSED(output);
    environment_t* env = (environment_t*)data;
    env->output.desc = desc;
}

static const struct wl_output_listener wl_output_listener = {
    .geometry = on_output_geometry,
    .mode = on_output_mode,
    .done = on_output_done,
    .scale = on_output_scale,
    .name = on_output_name,
    .description = on_output_description
};

// Seat callbacks ------------------------------------------------------------

static void on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_SURFACE | EVENT_FRAME_MOTIONS;
    env_pointer->frame.surface_changed = surface;
    env_pointer->frame.enter_serial = serial;

    env_pointer->frame.surface_local_x = x;
    env_pointer->frame.surface_local_y = y;
}

static void on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
    UNUSED(pointer);
    UNUSED(surface);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_SURFACE | EVENT_FRAME_MOTIONS;
    env_pointer->frame.surface_changed = NULL;
    env_pointer->frame.enter_serial = serial;

    env_pointer->frame.surface_local_x = 0;
    env_pointer->frame.surface_local_y = 0;
}

static void on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(time);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_MOTIONS;
    env_pointer->frame.surface_local_x = x;
    env_pointer->frame.surface_local_y = y;
}

static void on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    UNUSED(time);
    UNUSED(serial);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;

    uint32_t btn_mask = 0;
    if (button == BTN_LEFT) {
        btn_mask = config_get_pointer_left_button();
    } else if (button == BTN_RIGHT) {
        btn_mask = config_get_pointer_right_button();
    } else if (button == BTN_MIDDLE) {
        btn_mask = config_get_pointer_middle_button();
    } else {
        btn_mask = EVENT_FRAME_MISC_BUTTON;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        env_pointer->frame.buttons_pressed |= btn_mask;
        env_pointer->frame.buttons_released &= ~btn_mask;
    } else {
        env_pointer->frame.buttons_released |= btn_mask;
        env_pointer->frame.buttons_pressed &= ~btn_mask;
    }
}

static void on_pointer_axis_source(void* data, struct wl_pointer* pointer, uint32_t axis_source)
{
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis_source);
    return;
}

static void on_pointer_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis)
{
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(time);
    UNUSED(axis);
    return;
}

static void on_pointer_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t discrete)
{
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(discrete);
    return;
}

static void on_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    UNUSED(time);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(value);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_AXIS;
}

static void on_pointer_frame(void* data, struct wl_pointer* pointer)
{
    UNUSED(pointer);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    ACTIVATE_POINTER(env_pointer);

    environment_t* env = env_pointer->above_environment;
    environment_subsurface_t* env_subsurface = NULL;
    struct environment_callbacks* surface_pointer_callbacks = NULL;

    env_subsurface = environment_subsurface_from_surface(env_pointer->above_surface);
    surface_pointer_callbacks = wl_surface_get_callbacks(env_pointer->above_surface);

    // Workaround for hyprland
    if (env_pointer->grabbed_subsurface) {
        if ((env_pointer->frame.mask & (EVENT_FRAME_BUTTONS | EVENT_FRAME_SURFACE)) == (EVENT_FRAME_BUTTONS | EVENT_FRAME_SURFACE)) {
            if (env_pointer->above_surface == env_pointer->grabbed_subsurface->surface && env_pointer->frame.surface_changed) {
                env_pointer->frame.mask &= ~EVENT_FRAME_BUTTONS;
            }
        }
    }
    if (we_on_hyprland) {
        // Empty frame, sent on mascot release
        if (!env_pointer->frame.mask) {
            env_pointer->frame.mask = EVENT_FRAME_BUTTONS;
            env_pointer->frame.buttons_released = EVENT_FRAME_PRIMARY_BUTTON;
        }
    }

    // Once we leave the surface, we do not send any callbacks aside (leave)
    if (env_pointer->frame.mask & EVENT_FRAME_SURFACE) {
        if (surface_pointer_callbacks) {
            if (surface_pointer_callbacks->pointer_listener) {
                if (surface_pointer_callbacks->pointer_listener->leave) {
                    surface_pointer_callbacks->pointer_listener->leave((void*)env_pointer, NULL, env_pointer->enter_serial, env_pointer->above_surface);
                }
            }
        }

        env_pointer->above_surface = env_pointer->frame.surface_changed;
        env_pointer->enter_serial = env_pointer->frame.enter_serial;

        environment_t * new_env = environment_from_surface(env_pointer->above_surface);
        env_subsurface = environment_subsurface_from_surface(env_pointer->above_surface);
        surface_pointer_callbacks = wl_surface_get_callbacks(env_pointer->above_surface);
        if (new_env && new_env != env && env_pointer->grabbed_subsurface) {
            struct mascot * mascot = environment_subsurface_get_mascot(env_pointer->grabbed_subsurface);
            if (mascot) {
                mascot_environment_changed(mascot, new_env);
            }
            env = new_env;
        }
        if (new_env) env_pointer->above_environment = new_env;

        // Finally, send the enter callback for the new surface
        if (surface_pointer_callbacks) {
            if (surface_pointer_callbacks->pointer_listener) {
                if (surface_pointer_callbacks->pointer_listener->enter) {
                    surface_pointer_callbacks->pointer_listener->enter((void*)env_pointer, NULL, env_pointer->enter_serial, env_pointer->above_surface, env_pointer->frame.surface_local_x, env_pointer->frame.surface_local_y);
                }
            }
        }

    }

    if (env_pointer->frame.mask & EVENT_FRAME_MOTIONS) {
        if (surface_pointer_callbacks) {
            if (surface_pointer_callbacks->pointer_listener) {
                if (surface_pointer_callbacks->pointer_listener->motion) {
                    surface_pointer_callbacks->pointer_listener->motion((void*)env_pointer, NULL, 0, env_pointer->frame.surface_local_x, env_pointer->frame.surface_local_y);
                }
            }
        }
        env_pointer->surface_x = wl_fixed_to_int(env_pointer->frame.surface_local_x);
        env_pointer->surface_y = wl_fixed_to_int(env_pointer->frame.surface_local_y);

        if (env_pointer->grabbed_subsurface) {
            struct mascot * mascot = environment_subsurface_get_mascot(env_pointer->grabbed_subsurface);
            if (mascot) {
                environment_subsurface_move_to_pointer(env_pointer->grabbed_subsurface);
            }
        }
    }


    if (env_pointer->frame.mask & EVENT_FRAME_BUTTONS) {
        if (!env_pointer->above_surface && env_pointer->grabbed_subsurface) {
            // Impossible situation, but in case we somehow got button release after leave(), we should handle it
            surface_pointer_callbacks = wl_surface_get_callbacks(env_pointer->grabbed_subsurface->surface);
            env_subsurface = env_pointer->grabbed_subsurface;
            env = env_subsurface->env;

            env_pointer->above_surface = env_pointer->grabbed_subsurface->surface;
        }

        for (int i = 0; i < 4; i++) {
            int32_t button = 1 << i;
            if ((button & env_pointer->frame.buttons_released) && (button & env_pointer->buttons_state)) {
                if (surface_pointer_callbacks) {
                    if (surface_pointer_callbacks->pointer_listener) {
                        if (surface_pointer_callbacks->pointer_listener->button) {
                            surface_pointer_callbacks->pointer_listener->button((void*)env_pointer, NULL, 0, 0, button, WL_POINTER_BUTTON_STATE_RELEASED);
                        }
                    }
                }
                env_pointer->buttons_state &= ~button;
            }
            else if ((button & env_pointer->frame.buttons_pressed) && !(button & env_pointer->buttons_state)) {
                if (surface_pointer_callbacks) {
                    if (surface_pointer_callbacks) {
                        if (surface_pointer_callbacks->pointer_listener) {
                            if (surface_pointer_callbacks->pointer_listener->button) {
                                surface_pointer_callbacks->pointer_listener->button((void*)env_pointer, NULL, 0, 0, button, WL_POINTER_BUTTON_STATE_PRESSED);
                            }
                        }
                    }
                }
                env_pointer->buttons_state |= button;
            }
        }
    }


    if (env_pointer->frame.mask & EVENT_FRAME_AXIS) {
        if (surface_pointer_callbacks) {
            if (surface_pointer_callbacks->pointer_listener) {
                if (surface_pointer_callbacks->pointer_listener->axis) {
                    surface_pointer_callbacks->pointer_listener->axis((void*)env_pointer, NULL, 0, 0, 0);
                }
            }
        }
    }

    env_pointer->frame = (struct environment_event_frame){0};
}

static void on_pointer_axis_value120(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t value)
{
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(value);
    return ;
}

static void on_pointer_axis_relative_direction(void* data, struct wl_pointer* pointer, uint32_t axis, uint32_t direction)
{
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(direction);
    return ;
}

static void mascot_on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(serial);
    UNUSED(pointer);
    UNUSED(x);
    UNUSED(y);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in mascot_on_pointer_enter!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_enter!");
        return;
    }

    environment_t* env = environment_from_surface(surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_enter!");
        return;
    }

    environment_pointer_apply_cursor(env_pointer, -1);
}

static void mascot_on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(time);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in mascot_on_pointer_motion!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(env_pointer->above_surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_motion!");
        return;
    }

    struct mascot * mascot = environment_subsurface_get_mascot(env_surface);
    if (!mascot) {
        WARN("Unexpected! mascot_on_pointer_motion is called, but somehow we not above mascot???");
        return;
    }

    environment_t * env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_motion!");
        return;
    }

    int32_t global_x;
    int32_t global_y;

    // Transform surface_local coordinates to global coordinates
    // global_x = env_surface->pose->anchor_x + wl_fixed_to_int(x) + env_surface->mascot->X->value.i;
    // global_y = env_surface->pose->anchor_y + wl_fixed_to_int(y) + env_surface->mascot->Y->value.i;

    bool is_dragged_surface = false;
    if (env_pointer->grabbed_subsurface) {
        if (env_pointer->grabbed_subsurface->surface == env_pointer->above_surface) {
            is_dragged_surface = true;
        }
    }

    if (is_dragged_surface) {
        // Previous position
        global_x = env_pointer->x;
        global_y = env_pointer->y;

        global_x += wl_fixed_to_int(x) - env_pointer->surface_x;
        global_y += wl_fixed_to_int(y) - env_pointer->surface_y;
    } else {
        int32_t anchor_x = 0;
        int32_t anchor_y = 0;

        if (env_surface->pose) {
            anchor_x = env_surface->pose->anchor_x;
            anchor_y = env_surface->pose->anchor_y;
        }

        global_x = mascot->X->value.i + wl_fixed_to_int(x) + anchor_x / env_surface->env->scale;
        global_y = yconv(env_surface->env, mascot->Y->value.i) + wl_fixed_to_int(y) + anchor_y / env_surface->env->scale;
    }


    env_pointer->x = global_x;
    env_pointer->y = global_y;

    if (env_pointer->grabbed_subsurface) return;

    // Get hotspot
    struct mascot_hotspot* hotspot = mascot_hotspot_by_pos(env_surface->mascot, wl_fixed_to_int(env_pointer->frame.surface_local_x), wl_fixed_to_int(env_pointer->frame.surface_local_y));
    if (hotspot && !(env_pointer->grabbed_subsurface || env->select_active)) environment_pointer_apply_cursor(env_pointer, hotspot->cursor);
    else if (!hotspot && !(env_pointer->grabbed_subsurface || env->select_active)) environment_pointer_apply_cursor(env_pointer, -2);
    else environment_pointer_apply_cursor(env_pointer, -1);
}


static void environment_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{

    UNUSED(serial);
    UNUSED(time);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in environment_on_pointer_button!");
    }

    environment_t* env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in environment_on_pointer_button!");
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == EVENT_FRAME_PRIMARY_BUTTON) {
            if (env->select_active) {
                env->select_active = false;
                env->select_callback(env, env_pointer->x, yconv(env, env_pointer->y), environment_subsurface_from_surface(env_pointer->above_surface), env->select_data);
                environment_pointer_apply_cursor(env_pointer, -2);
            }
        }
    } else {
        if (button == EVENT_FRAME_PRIMARY_BUTTON) {
            if (env_pointer->grabbed_subsurface) {
                env_pointer->dx = (env_pointer->x - env_pointer->dx);
                env_pointer->dy = (env_pointer->y - env_pointer->dy);
                mascot_drag_ended(environment_subsurface_get_mascot(env_pointer->grabbed_subsurface), true);
            }
        }
    }
}

static void mascot_on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{

    UNUSED(serial);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in mascot_on_pointer_enter!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_enter!");
        return;
    }

    environment_t* env = environment_from_surface(surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_enter!");
        return;
    }

    // Apply cursors
    if (env_pointer->grabbed_subsurface || env->select_active) {
        mascot_hotspot_hold(env_surface->mascot, 0, 0, 0, true);
    }
}

static void mascot_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in mascot_on_pointer_enter!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(env_pointer->above_surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_enter!");
        return;
    }

    environment_t* env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_enter!");
        return;
    }

    struct mascot * mascot = environment_subsurface_get_mascot(env_surface);
    if (!mascot) {
        WARN("Unexpected lack of mascot while we are in mascot_on_pointer_button!");
        return;
    }

    int pressed_button = -1;
    if (button == EVENT_FRAME_PRIMARY_BUTTON) {
        pressed_button = mascot_hotspot_button_left;
    } else if (button == EVENT_FRAME_THIRD_BUTTON) {
        pressed_button = mascot_hotspot_button_middle;
    } else if (button == EVENT_FRAME_SECONDARY_BUTTON) {
        pressed_button = mascot_hotspot_button_right;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (!env->select_active && !env_pointer->grabbed_subsurface) {
            bool hotspot_press_result = mascot_hotspot_hold(mascot, env_pointer->surface_x, env_pointer->surface_y, pressed_button, false);
            if (!hotspot_press_result && pressed_button == mascot_hotspot_button_left) {
                mascot_drag_started(mascot, env_pointer);
            }
        } else {
            environment_on_pointer_button(data, pointer, serial, time, button, state);
        }

    } else {
        if (env_pointer->grabbed_subsurface && pressed_button == mascot_hotspot_button_left) {
            env_pointer->dx = (env_pointer->x - env_pointer->dx);
            env_pointer->dy = (env_pointer->y - env_pointer->dy);
            mascot_drag_ended(environment_subsurface_get_mascot(env_pointer->grabbed_subsurface), true);
        } else {
            if (mascot->hotspot_active || env_pointer->grabbed_subsurface) {
                mascot_hotspot_hold(mascot, env_pointer->surface_x, env_pointer->surface_y, pressed_button, true);
                struct mascot_hotspot* hotspot = mascot_hotspot_by_pos(mascot, env_pointer->surface_x, env_pointer->surface_y);
                if (hotspot) {
                    environment_pointer_apply_cursor(env_pointer, hotspot->cursor);
                } else {
                    environment_pointer_apply_cursor(env_pointer, -2);
                }
            } else {
                environment_pointer_apply_cursor(env_pointer, -2);
                environment_on_pointer_button(data, pointer, serial, time, button, state);
            }
        }
    }
}
static void mascot_on_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(time);
    UNUSED(value);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in mascot_on_pointer_enter!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(env_pointer->above_surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_enter!");
        return;
    }

    environment_t* env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_enter!");
        return;
    }

    struct mascot * mascot = environment_subsurface_get_mascot(env_surface);
    if (!mascot) {
        WARN("Unexpected lack of mascot while we are in mascot_on_pointer_button!");
        return;
    }

    if (env_surface) {
        mascot_hotspot_click(env_surface->mascot, env_pointer->surface_x, env_pointer->surface_y, mascot_hotspot_button_middle);
    }
}

static void environment_on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(serial);
    UNUSED(pointer);
    UNUSED(x);
    UNUSED(y);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in environment_on_pointer_enter!");
    }

    environment_t* env = environment_from_surface(surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in environment_on_pointer_enter!");
        return;
    }

    environment_pointer_apply_cursor(env_pointer, -1);
}

static void environment_on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(time);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in environment_on_pointer_motion!");
    }

    environment_t * env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in environment_on_pointer_motion!");
        return;
    }

    // Apply position as is
    env_pointer->x = wl_fixed_to_int(x);
    env_pointer->y = wl_fixed_to_int(y);

    environment_pointer_apply_cursor(env_pointer, -1);
}

static void environment_on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
    UNUSED(serial);
    UNUSED(pointer);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        ERROR("Critical! No pointer in environment_on_pointer_leave!");
    }

    environment_t* env = environment_from_surface(surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in environment_on_pointer_leave!");
        return;
    }

}

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = on_pointer_enter,
    .leave = on_pointer_leave,
    .motion = on_pointer_motion,
    .button = on_pointer_button,
    .axis = on_pointer_axis,
    .frame = on_pointer_frame,
    .axis_source = on_pointer_axis_source,
    .axis_stop = on_pointer_axis_stop,
    .axis_discrete = on_pointer_axis_discrete,
    .axis_value120 = on_pointer_axis_value120,
    .axis_relative_direction = on_pointer_axis_relative_direction,
};

static void on_seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
    UNUSED(seat);
    UNUSED(data);
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        struct wl_pointer* pointer = wl_seat_get_pointer(seat);

        // Create environment pointer
        environment_pointer_t* env_pointer = allocate_env_pointer(ENVIRONMENT_POINTER_PROTOCOL_POINTER, pointer);

        wl_pointer_add_listener(pointer, &wl_pointer_listener, env_pointer);
    }
    if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
        // struct wl_touch* touch = wl_seat_get_touch(seat);
        // environment_pointer_t* env_pointer = allocate_env_pointer(ENVIRONMENT_POINTER_PROTOCOL_TOUCH, touch);

        // wl_touch_add_listener(touch, &wl_touch_listener, env_pointer);
    }
}

static void on_seat_name(void* data, struct wl_seat* seat, const char* name)
{
    UNUSED(seat);
    UNUSED(name);
    UNUSED(data);
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = on_seat_capabilities,
    .name = on_seat_name
};

// Tablet ---------------------------------------------------------------------

static void on_tool_type(void* data, struct zwp_tablet_tool_v2* tool, uint32_t type)
{
    UNUSED(tool);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->aux_data = (void*)(uintptr_t)type;
}

static void on_tool_hardware_serial(void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(hi);
    UNUSED(lo);
}

static void on_tool_hardware_id_wacom(void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(hi);
    UNUSED(lo);
}

static void on_tool_capability(void* data, struct zwp_tablet_tool_v2* tool, uint32_t capability)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(capability);
}

static void on_tool_done(void* data, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(data);
    UNUSED(tool);
}

static void on_tool_removed(void* data, struct zwp_tablet_tool_v2* tool)
{
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    if (env_pointer->grabbed_subsurface) {
        env_pointer->dx = (env_pointer->x - env_pointer->dx);
        env_pointer->dy = (env_pointer->y - env_pointer->dy);
        mascot_drag_ended(env_pointer->grabbed_subsurface->mascot, true);
    }

    struct wl_pointer_listener* surface_pointer_callbacks = wl_surface_get_callbacks(env_pointer->above_surface);

    if (surface_pointer_callbacks) {

        for (int i = 0; i < 4; i++) {
            int32_t button = 1 << i;
            if (button & env_pointer->buttons_state) {
                if (surface_pointer_callbacks->button) {
                    surface_pointer_callbacks->button((void*)env_pointer, NULL, 0, 0, button, WL_POINTER_BUTTON_STATE_RELEASED);
                }
            }
        }

        if (surface_pointer_callbacks->leave) {
            surface_pointer_callbacks->leave((void*)env_pointer, NULL, env_pointer->enter_serial, env_pointer->above_surface);
        }
    }

    env_pointer->above_surface = NULL;

    free_env_pointer(env_pointer);
    zwp_tablet_tool_v2_set_user_data(tool, NULL);
    zwp_tablet_tool_v2_destroy(tool);
}

static void on_tool_proximity_in(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, struct zwp_tablet_v2* tablet, struct wl_surface* surface)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(surface);
    UNUSED(tablet);
    UNUSED(serial);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_SURFACE | EVENT_FRAME_PROXIMITY;
    env_pointer->frame.surface_changed = surface;
    env_pointer->frame.enter_serial = serial;

}

static void on_tool_proximity_out(void* data, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(data);
    UNUSED(tool);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    if (env_pointer->grabbed_subsurface && why_tablet_v2_proximity_in_events_received_by_parent_question_mark) {
        env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;
        env_pointer->frame.buttons_released |= EVENT_FRAME_PRIMARY_BUTTON;
    }

    env_pointer->frame.mask |= EVENT_FRAME_SURFACE;
    env_pointer->frame.surface_changed = NULL;
    env_pointer->frame.enter_serial = 0;
}

static void on_tool_down(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial)
{
    UNUSED(tool);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    uint32_t button_mask = 0;
    uint64_t tool_type = (uintptr_t)env_pointer->aux_data;

    if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_PEN) {
        button_mask = config_get_on_tool_pen();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_ERASER) {
        button_mask = config_get_on_tool_eraser();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_BRUSH) {
        button_mask = config_get_on_tool_brush();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_PENCIL) {
        button_mask = config_get_on_tool_pencil();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH) {
        button_mask = config_get_on_tool_airbrush();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE) {
        button_mask = config_get_on_tool_mouse();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_LENS) {
        button_mask = config_get_on_tool_lens();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_FINGER) {
        button_mask = config_get_on_tool_finger();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE) {
        button_mask = config_get_on_tool_mouse();
    } else {
        button_mask = EVENT_FRAME_MISC_BUTTON;
    }

    env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;
    env_pointer->frame.buttons_pressed |= button_mask;
    env_pointer->frame.buttons_released &= ~button_mask;
    env_pointer->frame.enter_serial = serial;
}

static void on_tool_up(void* data, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(tool);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    uint32_t button_mask = 0;
    uint64_t tool_type = (uintptr_t)env_pointer->aux_data;

    if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_PEN) {
        button_mask = config_get_on_tool_pen();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_ERASER) {
        button_mask = config_get_on_tool_eraser();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_BRUSH) {
        button_mask = config_get_on_tool_brush();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_PENCIL) {
        button_mask = config_get_on_tool_pencil();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH) {
        button_mask = config_get_on_tool_airbrush();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE) {
        button_mask = config_get_on_tool_mouse();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_LENS) {
        button_mask = config_get_on_tool_lens();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_FINGER) {
        button_mask = config_get_on_tool_finger();
    } else if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE) {
        button_mask = config_get_on_tool_mouse();
    } else {
        button_mask = EVENT_FRAME_MISC_BUTTON;
    }

    env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;
    env_pointer->frame.buttons_released |= button_mask;
    env_pointer->frame.buttons_pressed &= ~button_mask;

}

static void on_tool_button(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, uint32_t button, uint32_t state)
{
    UNUSED(tool);
    UNUSED(serial);
    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    uint32_t btn_mask = 0;
    uint64_t tool_type = (uintptr_t)env_pointer->aux_data;
    if (tool_type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE) {
        if (button == BTN_LEFT) {
            btn_mask = config_get_pointer_left_button();
        } else if (button == BTN_RIGHT) {
            btn_mask = config_get_pointer_right_button();
        } else if (button == BTN_MIDDLE) {
            btn_mask = config_get_pointer_middle_button();
        }
    }

    if (!btn_mask) {
        if (button == BTN_STYLUS) {
            btn_mask = config_get_on_tool_button1();
        } else if (button == BTN_STYLUS2) {
            btn_mask = config_get_on_tool_button2();
        } else if (button == BTN_STYLUS3) {
            btn_mask = config_get_on_tool_button3();
        }
    }

    env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        env_pointer->frame.buttons_pressed |= btn_mask;
        env_pointer->frame.buttons_released &= ~btn_mask;
    } else {
        env_pointer->frame.buttons_released |= btn_mask;
        env_pointer->frame.buttons_pressed &= ~btn_mask;
    }
}

static void on_tool_motion(void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(tool);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    env_pointer->frame.mask |= EVENT_FRAME_MOTIONS;
    env_pointer->frame.surface_local_x = x;
    env_pointer->frame.surface_local_y = y;

    if (why_tablet_v2_proximity_in_events_received_by_parent_question_mark) {
        if (!env_pointer->grabbed_subsurface) {
            // Now our coordinates is in global space, we to map them back to surface space
            int32_t global_x = wl_fixed_to_int(x);
            int32_t global_y = wl_fixed_to_int(y);

            int32_t local_x;
            int32_t local_y;

            environment_t* env = environment_from_surface(env_pointer->above_surface);
            if (!env) return;

            struct environment_subsurface* env_subsurface = environment_subsurface_from_surface(env_pointer->above_surface);
            if (!env_subsurface) return;

            struct mascot* mascot = env_subsurface->mascot;
            if (!mascot) return;

            int32_t anchor_x = 0;
            int32_t anchor_y = 0;

            if (env_subsurface->pose) {
                anchor_x = env_subsurface->pose->anchor_x;
                anchor_y = env_subsurface->pose->anchor_y;
            }

            // Transform global coordinates to surface_local coordinates
            local_x = global_x - anchor_x * env->scale - mascot->X->value.i;
            local_y = global_y - anchor_y * env->scale - yconv(env, mascot->Y->value.i);

            env_pointer->frame.surface_local_x = wl_fixed_from_int(local_x);
            env_pointer->frame.surface_local_y = wl_fixed_from_int(local_y);
        } else {
            environment_t* env = environment_from_surface(env_pointer->above_surface);
            if (!env) return;
            if (env_pointer->above_surface == env_pointer->grabbed_subsurface->surface) {
                env_pointer->frame.mask |= EVENT_FRAME_SURFACE;
                env_pointer->frame.surface_changed = env->root_surface->surface;
                env_pointer->frame.enter_serial = env_pointer->enter_serial;
            }
        }
    }

}

static void on_tool_pressure(void* data, struct zwp_tablet_tool_v2* tool, uint32_t pressure)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(pressure);
}

static void on_tool_distance(void* data, struct zwp_tablet_tool_v2* tool, uint32_t distance)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(distance);
}

static void on_tool_tilt(void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(x);
    UNUSED(y);
}

static void on_tool_frame(void* data, struct zwp_tablet_tool_v2* tool, uint32_t time)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(time);

    environment_pointer_t* env_pointer = (environment_pointer_t*)data;
    if (!env_pointer) {
        return;
    }

    // Workaround for KDE
    if (env_pointer->frame.mask & EVENT_FRAME_PROXIMITY) {
        if (env_pointer->frame.mask & EVENT_FRAME_SURFACE) {
            environment_t* env = environment_from_surface(env_pointer->frame.surface_changed);
            if (!env) {
                return;
            }

            struct wl_surface* surface = env_pointer->frame.surface_changed;

            if (!env->select_active && is_root_surface(surface)) {
                if (!why_tablet_v2_proximity_in_events_received_by_parent_question_mark && mascot_by_coordinates && !disable_tablet_workarounds) {
                    why_tablet_v2_proximity_in_events_received_by_parent_question_mark = true;
                    WARN("WORKAROUND: zwp_tablet_v2 proximity_in events are received by parent in KDE, not by subsurface. Applying stupid workaround.");
                }
                if (mascot_by_coordinates) {
                    struct mascot* mascot = mascot_by_coordinates(env, wl_fixed_to_int(env_pointer->frame.surface_local_x), wl_fixed_to_int(env_pointer->frame.surface_local_y));
                    if (mascot) {
                        env_pointer->frame.surface_changed = mascot->subsurface->surface;
                    }
                }
            }
        }
    }


    // Run pointer's frame, we do not implement this again;
    on_pointer_frame(data, NULL);
}

static const struct zwp_tablet_tool_v2_listener zwp_tablet_tool_v2_listener = {
    .type = on_tool_type,
    .hardware_serial = on_tool_hardware_serial,
    .hardware_id_wacom = on_tool_hardware_id_wacom,
    .capability = on_tool_capability,
    .done = on_tool_done,
    .removed = on_tool_removed,
    .proximity_in = on_tool_proximity_in,
    .proximity_out = on_tool_proximity_out,
    .down = on_tool_down,
    .up = on_tool_up,
    .button = on_tool_button,
    .motion = on_tool_motion,
    .pressure = on_tool_pressure,
    .distance = on_tool_distance,
    .tilt = on_tool_tilt,
    .frame = on_tool_frame
};

static void on_tablet_added(void* data, struct zwp_tablet_seat_v2* tablet_seat, struct zwp_tablet_v2* tablet)
{
    UNUSED(data);
    UNUSED(tablet_seat);
    UNUSED(tablet);
}

static void on_tool_added(void* data, struct zwp_tablet_seat_v2* tablet_seat, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(data);
    UNUSED(tablet_seat);

    environment_pointer_t* env_pointer = allocate_env_pointer(ENVIRONMENT_POINTER_PROTOCOL_TABLET, tool);
    zwp_tablet_tool_v2_add_listener(tool, &zwp_tablet_tool_v2_listener, env_pointer);
}

static void on_pad_added(void* data, struct zwp_tablet_seat_v2* tablet_seat, struct zwp_tablet_pad_v2* pad)
{
    UNUSED(data);
    UNUSED(tablet_seat);
    UNUSED(pad);
}

struct zwp_tablet_seat_v2_listener zwp_tablet_seat_v2_listener = {
    .tablet_added = on_tablet_added,
    .tool_added = on_tool_added,
    .pad_added = on_pad_added,
};

// Fractional scale callbacks

static void on_preffered_scale(void* data, struct wp_fractional_scale_v1* wp_fractional_scale_v1, uint32_t scale)
{
    UNUSED(wp_fractional_scale_v1);
    environment_t* env = data;
    env->scale = scale / 120.0;
}

// XDG Output callbacks
static void xdg_output_logical_position(void* data, struct zxdg_output_v1* xdg_output, int32_t x, int32_t y)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = true;
    env->lx = x;
    env->ly = y;
}

static void xdg_output_logical_size(void* data, struct zxdg_output_v1* xdg_output, int32_t width, int32_t height)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = true;
    env->lwidth = width;
    env->lheight = height;
}

static void xdg_output_name(void* data, struct zxdg_output_v1* xdg_output, const char* name)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = true;
    env->output.name = strdup(name);
}

static void xdg_output_description(void* data, struct zxdg_output_v1* xdg_output, const char* description)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = true;
    env->output.desc = strdup(description);
}

static void xdg_output_done(void* data, struct zxdg_output_v1* xdg_output)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = true;
    INFO("Environment id %d is ready. lpos (%d,%d), lsize (%d,%d)", env->id, env->lx, env->ly, env->lwidth, env->lheight);
}

// Registry callbacks ---------------------------------------------------------

static void handle_viewporter(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded wp_viewporter global of ver %u", version);
    viewporter = wl_registry_bind(registry, id, &wp_viewporter_interface, version);
}

static void handle_fractional_scale_manager(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded wp_viewporter global of ver %u", version);
    fractional_manager = wl_registry_bind(registry, id, &wp_fractional_scale_manager_v1_interface, version);
}

static void handle_compositor(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded wl_compositor global of ver %u", version);
    compositor = wl_registry_bind(registry, id, &wl_compositor_interface, version);
}

static void handle_shm_manager(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded wl_shm global of ver %u", version);
    shm_manager = wl_registry_bind(registry, id, &wl_shm_interface, version);
    wl_shm_add_listener(shm_manager, &wl_shm_listener_cb, NULL);
}

static void handle_subcompositor(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded wl_subcompositor global of ver %u", version);
    subcompositor = wl_registry_bind(registry, id, &wl_subcompositor_interface, version);
}

static void handle_wlr_layer_shell(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded zwlr_layer_shell_v1 global of ver %u", version);
    wlr_layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, version);
}

static void handle_tablet_manager(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    DEBUG("Binded zwp_tablet_manager_v2 global of ver %u", version);
    tablet_manager = wl_registry_bind(registry, id, &zwp_tablet_manager_v2_interface, version);
}

static void handle_output(void* data, uint32_t id, uint32_t version)
{
    struct envs_queue* envs = (struct envs_queue*)data;
    struct wl_output* output = wl_registry_bind(registry, id, &wl_output_interface, version);
    if (new_environment) {
        environment_t* env = (environment_t*)calloc(1, sizeof(environment_t));
        env->id = wl_refcounter++;
        env->output.output = output;
        env->output.id = display_id++;
        env->referenced_mascots = list_init(256);
        wl_output_add_listener(output, &wl_output_listener, (void*)env);
        wl_output_set_user_data(output, (void*)env);
        new_environment(env);
        envs->envs[envs->envs_count++] = env;
        if (envs->post_init) {
            dispatch_envs_queue(envs);
            envs->envs_count = 0;
            envs->envs[0] = NULL;
        }
    }
    DEBUG("Binded wl_output global of ver %u", version);
}

static void handle_seat(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    seat = wl_registry_bind(registry, id, &wl_seat_interface, version);
    wl_seat_add_listener(seat, &wl_seat_listener, NULL);
    DEBUG("Binded wl_seat global of ver %u", version);
}

static void handle_cursor_shape(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    cursor_shape_manager = wl_registry_bind(registry, id, &wp_cursor_shape_manager_v1_interface, version);
    DEBUG("Binded cursor_shape global of ver %u", version);
}

static void handle_xdg_output_manager(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    xdg_output_manager = wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface, version);
    INFO("Binded xdg_output_manager global of ver %u", version);
}

static void on_global (void* data, struct wl_registry* registry, uint32_t id, const char* iface_name, uint32_t version)
{
    UNUSED(registry);

    struct envs_queue* envs = (struct envs_queue*)data;

    if (!strcmp(iface_name, wl_compositor_interface.name)) {
        handle_compositor(data, id, version);
    } else if (!strcmp(iface_name, wl_shm_interface.name)) {
        handle_shm_manager(data, id, version);
    } else if (!strcmp(iface_name, wl_subcompositor_interface.name)) {
        handle_subcompositor(data, id, version);
    } else if (!strcmp(iface_name, zwlr_layer_shell_v1_interface.name)) {
        handle_wlr_layer_shell(data, id, version);
    } else if (!strcmp(iface_name, zwp_tablet_manager_v2_interface.name) && !(envs->flags & ENV_DISABLE_TABLETS)) {
        handle_tablet_manager(data, id, version);
    } else if (!strcmp(iface_name, wl_output_interface.name)) {
        handle_output(data, id, version);
    } else if (!strcmp(iface_name, wl_seat_interface.name)) {
        handle_seat(data, id, version);
    } else if (!strcmp(iface_name, wp_viewporter_interface.name) && !(envs->flags & ENV_DISABLE_VIEWPORTER)) {
        handle_viewporter(data, id, version);
    } else if (!strcmp(iface_name, wp_fractional_scale_manager_v1_interface.name) && !(envs->flags & ENV_DISABLE_FRACTIONAL_SCALE)) {
        handle_fractional_scale_manager(data, id, version);
    } else if (!strcmp(iface_name, wp_cursor_shape_manager_v1_interface.name) && !(envs->flags & ENV_DISABLE_CURSOR_SHAPE)) {
        handle_cursor_shape(data, id, version);
    } else if (!strcmp(iface_name, zxdg_output_manager_v1_interface.name)) {
        handle_xdg_output_manager(data, id, version);
    }
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
	UNUSED(data);
    UNUSED(registry);
    UNUSED(id);
}

static const struct wl_registry_listener registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove
};

// Public functions ------------------------------------------------------------

enum environment_init_status environment_init(int flags,
    void(*new_listener)(environment_t*), void(*rem_listener)(environment_t*),
    void(*orph_listener)(struct mascot*), void(*mascot_dropped_oob_listener)(struct mascot*, int32_t, int32_t)
)
{

    const char* session = getenv("XDG_CURRENT_DESKTOP");
    if (session) {
        if (!strcmp(session, "KDE") && !disable_tablet_workarounds) {
            WARN("KDE session detected, applying workaround for tablet proximity_in events");
            why_tablet_v2_proximity_in_events_received_by_parent_question_mark = true;
            we_on_kde = true;
        }
        if (!strcmp(session, "Hyprland")) {
            WARN("Hyprland session detected, applying workaround for mascot dragging");
            we_on_hyprland = true;
        }
    }

    // Wayland display connection and etc
    display = wl_display_connect(NULL);
    if (!display) {
        snprintf(error_m, 1024, "Environment initialization failed: Failed to connect to Wayland display");
        init_status = ENV_INIT_ERROR_DISPLAY;
        return ENV_INIT_ERROR_DISPLAY;
    }

    new_environment = new_listener;
    rem_environment = rem_listener;
    orphaned_mascot = orph_listener;
    mascot_dropped_oob = mascot_dropped_oob_listener;

    struct envs_queue* envs = (struct envs_queue*)calloc(1, sizeof(struct envs_queue));
    envs->flags = flags;

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, (void*)envs);
    block_until_synced();

    // Check if all required globals are present
    if (!compositor) {
        error_offt += snprintf(error_m, 1024, "Required global is missing: wl_compositor.\n");
        init_status = ENV_INIT_ERROR_GLOBALS;
    }
    if (!shm_manager) {
        error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Required global is missing: wl_shm.\n");
        init_status = ENV_INIT_ERROR_GLOBALS;
    }
    if (!subcompositor) {
        error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Required global is missing: wl_subcompositor\nNote: Following global is crucial for the correct operation of the program.\n");
        init_status = ENV_INIT_ERROR_GLOBALS;
    }
    if (!wlr_layer_shell) {
        error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Required protocol is missing: wlr_layer_shell.\nNote: Following global is base of the program.\n");
        init_status = ENV_INIT_ERROR_GLOBALS;
    }
    if (init_status != ENV_NOT_INITIALIZED) {
        return init_status;
    }

    empty_region = wl_compositor_create_region(compositor);

    if (!empty_region) {
        snprintf(error_m, 1024, "Environment initialization failed: Failed to create empty region");
        init_status = ENV_INIT_ERROR_GENERIC;
        return ENV_INIT_ERROR_GENERIC;
    }

    wl_region_subtract(empty_region, 0, 0, INT32_MAX, INT32_MAX);

    dispatch_envs_queue(envs);
    envs->post_init = true;
    envs->envs_count = 0;
    memset(envs->envs, 0, sizeof(envs->envs));

    if (tablet_manager && !(flags & ENV_DISABLE_TABLETS)) {
        tablet_seat = zwp_tablet_manager_v2_get_tablet_seat(tablet_manager, seat);
        if (tablet_seat) {
            zwp_tablet_seat_v2_add_listener(tablet_seat, &zwp_tablet_seat_v2_listener, NULL);
        } else {
            WARN("Failed to get tablet seat");
        }
    }

    init_status = ENV_INIT_OK;
    return init_status;
}

int environment_dispatch()
{
    return wl_display_dispatch(display);
}

void environment_unlink(environment_t *env)
{

    for (uint32_t i = 0; i < list_size(env->referenced_mascots); i++) {
        struct mascot* mascot = list_get(env->referenced_mascots, i);
        if (mascot) {
            if (orphaned_mascot) {
                orphaned_mascot(mascot);
            }
        }
    }

    list_free(env->referenced_mascots);

    if (env->xdg_output) {
        zxdg_output_v1_destroy(env->xdg_output);
    }

    if (env->root_environment_subsurface) {
        environment_destroy_subsurface(env->root_environment_subsurface);
    }
    if (env->root_surface) {
        layer_surface_destroy(env->root_surface);
    }

    if (env->output.output) {
        wl_output_destroy(env->output.output);
    }
    free(env);
}

void environment_new_env_listener(void(*listener)(environment_t*))
{
    new_environment = listener;
}
void environment_rem_env_listener(void(*listener)(environment_t*))
{
    rem_environment = listener;
}

// Surface API -----------------------------------------------------------------

environment_subsurface_t* environment_create_subsurface(environment_t* env)
{
    if (!env->is_ready) return NULL;

    environment_subsurface_t* subsurface = (environment_subsurface_t*)calloc(1, sizeof(environment_subsurface_t));
    if (!subsurface) return NULL;

    subsurface->surface = wl_compositor_create_surface(compositor);
    if (!subsurface->surface) {
        free(subsurface);
        return NULL;
    }

    wl_surface_set_data(subsurface->surface, WL_SURFACE_ROLE_SUBSURFACE, subsurface, env);

    subsurface->subsurface = wl_subcompositor_get_subsurface(subcompositor, subsurface->surface, env->root_surface->surface);
    if (!subsurface->subsurface) {
        wl_surface_clear_data(subsurface->surface);
        wl_surface_destroy(subsurface->surface);
        free(subsurface);
        return NULL;
    }

    struct environment_callbacks* callbacks = (struct environment_callbacks*)calloc(1, sizeof(struct environment_callbacks));
    if (!callbacks) {
        wl_surface_clear_data(subsurface->surface);
        wl_surface_destroy(subsurface->surface);
        free(subsurface);
        return NULL;
    }
    callbacks->data = (void*)subsurface;
    callbacks->pointer_listener = &mascot_pointer_listener;

    if (viewporter) {
        subsurface->viewport = wp_viewporter_get_viewport(viewporter, subsurface->surface);
        if (!subsurface->viewport) {
            wl_surface_clear_data(subsurface->surface);
            wl_surface_destroy(subsurface->surface);
            free(subsurface);
            return NULL;
        }
    }

    if (env->output.scale && !fractional_manager && !viewporter) {
        wl_surface_set_buffer_scale(subsurface->surface, env->output.scale);
    }

    wl_surface_attach_callbacks(subsurface->surface, callbacks);

    env->pending_commit = true;

    subsurface->env = env;
    return subsurface;
}

void environment_destroy_subsurface(environment_subsurface_t* surface)
{
    if (surface->fractional_scale) {
        wp_fractional_scale_v1_destroy(surface->fractional_scale);
    }

    if (surface->viewport) {
        wp_viewport_destroy(surface->viewport);
    }

    if (surface->surface) {
        wl_surface_clear_data(surface->surface);
    }

    if (surface->subsurface) {
        wl_subsurface_destroy(surface->subsurface);
    }

    if (surface->surface) {
        wl_surface_destroy(surface->surface);
    }

    surface->env->pending_commit = true;

    uint32_t mascot_index = list_find(surface->env->referenced_mascots, surface->mascot);
    if (mascot_index != UINT32_MAX) {
        list_remove(surface->env->referenced_mascots, mascot_index);
    }

    free(surface);
}

void environment_subsurface_attach(environment_subsurface_t* surface, const struct mascot_pose* pose)
{
    if (!surface->surface) return;
    if (!pose) {
        environment_subsurface_unmap(surface);
        return;
    }

    struct mascot_sprite* sprite = pose->sprite[surface->mascot->LookingRight->value.i];
    if (!sprite) {
        environment_subsurface_unmap(surface);
        return;
    }

    wl_surface_attach(surface->surface, sprite->buffer->buffer, 0, 0);
    wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
    if (!surface->drag_pointer) {
        wl_surface_set_input_region(surface->surface, sprite->buffer->region);
    }

    if (!surface->pose) {
        wl_subsurface_place_below(surface->subsurface, surface->env->root_environment_subsurface->surface);
        if (surface->mascot) {
            environment_subsurface_set_position(surface, surface->mascot->X->value.i, yconv(surface->env, surface->mascot->Y->value.i));
        }
        wl_surface_commit(surface->surface);
    }
    surface->pose = pose;

    surface->width = sprite->width;
    surface->height = sprite->height;

    wl_surface_commit(surface->surface);

    if (viewporter) {
        wp_viewport_set_destination(
            surface->viewport,
            sprite->width / surface->env->scale,
            sprite->height / surface->env->scale
        );
        wl_surface_commit(surface->surface);
    } else {
        wl_surface_set_buffer_scale(surface->surface, surface->env->scale);
    }

    environment_subsurface_set_position(surface, surface->mascot->X->value.i, yconv(surface->env, surface->mascot->Y->value.i));

    surface->env->pending_commit = true;
}

enum environment_move_result environment_subsurface_move(environment_subsurface_t* surface, int32_t dx, int32_t dy, bool use_callback)
{
    if (!surface->surface) return environment_move_invalid;

    enum environment_move_result result = environment_move_ok;

    if (dy < 0) {
        dy = 0;
        result = environment_move_clamped;
    } else if (dy > (int32_t)environment_workarea_height(surface->env)) {
        dy = environment_workarea_height(surface->env);
        result = environment_move_clamped;
    }

    if (dx < 0) {
        dx = 0;
        result = environment_move_clamped;
    } else if (dx > (int32_t)environment_workarea_width(surface->env)) {
        dx = environment_workarea_width(surface->env);
        result = environment_move_clamped;
    }

    dy = yconv(surface->env, dy);

    bool has_active_ie = false;
    if (environment_get_ie(surface->env)) {
        has_active_ie = surface->env->ie->active;
    }

    int current_x = dx;
    int current_y = dy;
    if (surface->mascot) {
        current_x = surface->mascot->X->value.i;
        current_y = yconv(surface->env, surface->mascot->Y->value.i);
    }

    int32_t ie_out_x = dx;
    int32_t ie_out_y = dy;

    enum environment_border_type ie_border = environment_try_ie_collision(surface->env, current_x, current_y, dx, dy, &ie_out_x, &ie_out_y);

    if (ie_border != environment_border_type_invalid && has_active_ie && surface->mascot->state != mascot_state_ie_walk && surface->mascot->state != mascot_state_ie_fall && surface->mascot->state != mascot_state_ie_throw) {
        if (!mascot_is_on_workspace_border(surface->mascot)) {
            if (!surface->mascot->associated_ie) {
                plugin_execute_ie_attach_mascot(surface->env->ie->parent_plugin, surface->env->ie, surface->mascot);
                result = environment_move_clamped;
            } else {
                if (current_x == dx && current_y != dy) {
                    ie_out_y = dy;
                } else if (current_x != dx && current_y == dy) {
                    ie_out_x = dx;
                }
            }
            dx = ie_out_x;
            dy = ie_out_y;
        }
    } else if (surface->mascot->associated_ie) {
        plugin_execute_ie_detach_mascot(surface->env->ie->parent_plugin, surface->env->ie, surface->mascot);
    }

    if (environment_subsurface_set_position(surface, dx, dy) == environment_move_invalid) {
        return environment_move_invalid;
    }

    if (surface->mascot && use_callback) {
        mascot_moved(surface->mascot, dx, yconv(surface->env, dy));
    }

    return result;
}

environment_t* environment_subsurface_get_environment(environment_subsurface_t* surface)
{
    if (!surface) return NULL;
    return surface->env;
}

// bool environment_subsurface_move_to_pointer(environment_subsurface_t* surface);

struct wl_compositor* environment_get_compositor()
{
    return compositor;
}
struct wl_shm* environment_get_shm()
{
    return shm_manager;
}

void environment_subsurface_drag(environment_subsurface_t* surface, environment_pointer_t* pointer) {
    if (!surface) return;
    if (!surface->surface) return;
    if (!surface->pose) return;
    if (!surface->env) return;
    if (!pointer) {
        environment_subsurface_release(surface);
        return;
    }
    surface->is_grabbed = true;
    enable_input_on_environments(true);
    wl_surface_set_input_region(surface->surface, empty_region);
    wl_subsurface_place_above(surface->subsurface, surface->env->root_environment_subsurface->surface);
    surface->drag_pointer = pointer;
    pointer->grabbed_subsurface = surface;
    pointer->above_environment = surface->env;

    environment_pointer_apply_cursor(pointer, mascot_hotspot_cursor_hand);

    pointer->dx = pointer->x;
    pointer->dy = pointer->y;
    pointer->reference_tick = 0;

    surface->env->pending_commit = true;
}
void environment_subsurface_release(environment_subsurface_t* surface) {
    if (!surface) return;
    if (!surface->surface) return;
    if (!surface->pose) return;
    if (!surface->env) return;
    if (!surface->drag_pointer) return;

    surface->is_grabbed = false;
    enable_input_on_environments(false);
    wl_surface_set_input_region(surface->surface, surface->pose->sprite[surface->mascot->LookingRight->value.i]->buffer->region);
    wl_subsurface_place_below(surface->subsurface, surface->env->root_environment_subsurface->surface);

    environment_pointer_t* drag_pointer = surface->drag_pointer;
    drag_pointer->grabbed_subsurface = NULL;
    drag_pointer->above_environment = NULL;
    environment_pointer_apply_cursor(drag_pointer, -2);

    if (drag_pointer->x < 0 || drag_pointer->y < 0 || drag_pointer->x > (int32_t)environment_workarea_width(surface->env) || drag_pointer->y > (int32_t)environment_workarea_height(surface->env)) {
        if (mascot_dropped_oob) {
            mascot_dropped_oob(surface->mascot, drag_pointer->x, drag_pointer->y);
        }
    }

    surface->env->pending_commit = true;
    surface->drag_pointer = NULL;
}

void environment_pointer_update_delta(environment_subsurface_t* subsurface, uint32_t tick)
{
    if (!subsurface) return;
    if (!subsurface->drag_pointer) return;
    environment_pointer_t* pointer = subsurface->drag_pointer;
    if (pointer->reference_tick+1 < tick) {
        pointer->dx = pointer->x;
        pointer->dy = pointer->y;
        pointer->reference_tick = tick;
    }
}

bool environment_subsurface_move_to_pointer(environment_subsurface_t* surface) {
    if (!surface) return false;
    if (!surface->surface) return false;
    if (!surface->pose) return false;
    if (!surface->env) return false;
    if (!surface->drag_pointer) return false;
    if (!surface->is_grabbed) return false;

    environment_subsurface_set_position(surface, surface->drag_pointer->x, surface->drag_pointer->y);
    if (surface->mascot) mascot_moved(surface->mascot, surface->drag_pointer->x, yconv(surface->env, surface->drag_pointer->y));
    environment_commit(surface->env);
    return true;
}

int32_t environment_cursor_x(struct mascot* mascot, environment_t* env) {
    UNUSED(env);
    environment_pointer_t* pointer = mascot ? mascot->subsurface->drag_pointer : active_pointer;
    if (!pointer) pointer = active_pointer;
    if (!pointer) return 0;
    return pointer->grabbed_subsurface ? pointer->x : pointer->public_x;
}

int32_t environment_cursor_y(struct mascot* mascot, environment_t* env) {
    UNUSED(env);
    environment_pointer_t* pointer = mascot ? mascot->subsurface->drag_pointer : active_pointer;
    if (!pointer) pointer = active_pointer;
    if (!pointer) return 0;
    return pointer->grabbed_subsurface ? pointer->y : pointer->public_y;
}

int32_t environment_cursor_dx(struct mascot* mascot, environment_t* env) {
    UNUSED(env);
    environment_pointer_t* pointer = mascot ? mascot->subsurface->drag_pointer : active_pointer;
    if (!pointer) pointer = active_pointer;
    if (!pointer) return 0;
    return pointer->dx;
}

int32_t environment_cursor_dy(struct mascot* mascot, environment_t* env) {
    UNUSED(env);
    environment_pointer_t* pointer = mascot ? mascot->subsurface->drag_pointer : active_pointer;
    if (!pointer) pointer = active_pointer;
    if (!pointer) return 0;
    return -pointer->dy;
}


int32_t environment_cursor_get_tick_diff(environment_pointer_t* pointer, uint32_t tick)
{
    return tick - pointer->reference_tick;
}

void environment_subsurface_unmap(environment_subsurface_t *surface)
{
    if (!surface) return;
    if (!surface->surface) return;
    if (!surface->env) return;

    wl_surface_attach(surface->surface, NULL, 0, 0);
    wl_surface_commit(surface->surface);
    surface->env->pending_commit = true;
    surface->width = 0;
    surface->height = 0;
    surface->x = 0;
    surface->y = 0;
    surface->pose = NULL;
}

bool environment_is_ready(environment_t* env) { return env->is_ready; }
bool environment_commit(environment_t* env)
{
    if (!env->root_surface) return false;
    if (!env->is_ready) return false;
    if (!env->pending_commit) return false;

    wl_surface_commit(env->root_surface->surface);
    wl_display_flush(display);

    env->pending_commit = false;

    return true;
}

void enviroment_wait_until_ready(environment_t* env)
{
    while (!env->is_ready) {
        wl_display_roundtrip(display);
        if (environment_dispatch() == -1) {
            ERROR("Failed to dispatch Wayland events! error code: %d", wl_display_get_error(display));
        }
        if (env->root_surface)
        {
            if (env->root_surface->configure_serial) {
                env->is_ready = true;
            }
        }
    }
}

float environment_screen_scale(environment_t* env)
{
    return env->scale;
}

void environment_subsurface_associate_mascot(environment_subsurface_t* surface, struct mascot* mascot_ptr)
{
    if (!surface) return;
    surface->mascot = mascot_ptr;
    list_add(surface->env->referenced_mascots, mascot_ptr);
}

void environment_subsurface_set_offset(environment_subsurface_t* surface, int32_t x, int32_t y) {
    if (!surface) return;
    surface->offset_x = x;
    surface->offset_y = y;

}

void environment_select_position(environment_t* env, void (*callback)(environment_t*, int32_t, int32_t, environment_subsurface_t*, void*), void* data)
{
    env->select_callback = callback;
    env->select_active = callback != NULL;
    env->select_data = data;
    environment_pointer_apply_cursor(active_pointer, mascot_hotspot_cursor_crosshair);
}

void environment_set_input_state(environment_t* env, bool active)
{
    if (!env) return;
    if (!env->root_surface) return;

    layer_surface_enable_input(env->root_surface, active);
    env->pending_commit = true;

}

int environment_get_display_fd() {
    return wl_display_get_fd(display);
}

struct mascot* environment_subsurface_get_mascot(environment_subsurface_t* surface)
{
    return surface->mascot;
}

const struct mascot_pose* environment_subsurface_get_pose(environment_subsurface_t* surface)
{
    return surface->pose;
}

const char* environment_get_error() {
    if (init_status == ENV_NOT_INITIALIZED) return "environment_get_error() called before initialization attempt";
    if (init_status == ENV_INIT_OK) return "No error";
    return error_m;
}

void environment_set_public_cursor_position(environment_t* env, int32_t x, int32_t y)
{
    UNUSED(env);
    if (!active_pointer) return;
    active_pointer->public_x = x;
    active_pointer->public_y = y;
}

void environment_set_ie(environment_t* env, struct ie_object *ie)
{
    if (!env) return;
    env->ie = ie;
    env->interactive_window_count = ie ? 1 : 0;
}

bool environment_pre_tick(environment_t* env, uint32_t tick)
{
    if (!env) return false;
    if (env->ie) {
        enum plugin_execution_result exec_res = plugin_execute(env->ie->parent_plugin, env->ie, &active_pointer->public_x, &active_pointer->public_y, tick);
        if (exec_res != PLUGIN_EXEC_OK) {
            if (exec_res == PLUGIN_EXEC_SEGFAULT) {
                ERROR("Plugin execution failed with SEGFAULT");
            }
        }
    }
    return true;
}

bool environment_ie_allows_move(environment_t* env)
{
    if (!env) return false;
    if (!env->ie) return false;
    return env->ie->parent_plugin->effective_caps & PLUGIN_PROVIDES_IE_MOVE;
}

bool environment_ie_throw(environment_t* env, float x_velocity, float y_velocity, float gravity, uint32_t tick)
{
    if (!env) return false;
    if (!env->ie) return false;

    enum plugin_execution_result exec_res = plugin_execute_throw_ie(env->ie->parent_plugin, env->ie, x_velocity, y_velocity, gravity, tick);

    if (exec_res == PLUGIN_EXEC_OK) {
        return true;
    } else {
        if (exec_res == PLUGIN_EXEC_SEGFAULT) {
            ERROR("Plugin execution failed with SEGFAULT");
        }
        return false;
    }
}

bool environment_ie_stop_movement(environment_t* env)
{
    if (!env) return false;
    if (!env->ie) return false;

    enum plugin_execution_result exec_res = plugin_execute_stop_ie(env->ie->parent_plugin, env->ie);

    if (exec_res == PLUGIN_EXEC_OK) {
        return true;
    } else {
        if (exec_res == PLUGIN_EXEC_SEGFAULT) {
            ERROR("Plugin execution failed with SEGFAULT");
        }
        return false;
    }
}

bool environment_ie_restore(environment_t *env)
{
    if (!env) return false;
    if (!env->ie) return false;
    if (!env->ie->parent_plugin->execute_restore_ies) return false;

    enum plugin_execution_result exec_res = plugin_execute_restore_ies(env->ie->parent_plugin);
    if (exec_res == PLUGIN_EXEC_OK) {
        return true;
    } else {
        if (exec_res == PLUGIN_EXEC_SEGFAULT) {
            ERROR("Plugin execution failed with SEGFAULT");
        }
        return false;
    }
}

#endif

enum environment_move_result environment_ie_move(environment_t* env, int32_t dx, int32_t dy)
{
    if (!env) return environment_move_invalid;
    if (!env->ie) return environment_move_invalid;
    if (!(env->ie->parent_plugin->effective_caps & PLUGIN_PROVIDES_IE_MOVE)) return environment_move_invalid;

    struct ie_object* ie = env->ie;

    // Check if window is not in screen bounds
    // Right edge is beyond left edge of screen

    enum environment_move_result result = environment_move_ok;

    if (dx + ie->width > (int32_t)environment_screen_width(env)) result = environment_move_clamped;
    else if (dx < 0) result = environment_move_clamped;

    if (dx + ie->width < 0) result = environment_move_out_of_bounds;
    else if (dx > (int32_t)environment_screen_width(env)) result = environment_move_out_of_bounds;

    enum plugin_execution_result res = plugin_execute_ie_move(env->ie->parent_plugin, env->ie, dx, dy);
    if (res != PLUGIN_EXEC_OK) {
        if (res == PLUGIN_EXEC_SEGFAULT) {
            ERROR("Plugin execution failed with SEGFAULT");
        }
        else {
            WARN("Plugin execution failed with unknown error");
            return environment_move_invalid;
        }
    }
    return result;
}

struct ie_object* environment_get_ie(environment_t* env)
{
    if (!env) return NULL;
    if (!config_get_ie_interactions()) return NULL;
    return env->ie;
}

void environment_get_output_id_info(environment_t* env, const char** name, const char** make, const char** model, const char** desc, uint32_t *id)
{
    if (!env) return;
    if (name) *name = env->output.name;
    if (make) *make = env->output.make;
    if (model) *model = env->output.model;
    if (desc) *desc = env->output.desc;
    if (id) *id = env->output.id;
}

enum environment_move_result environment_subsurface_set_position(environment_subsurface_t* surface, int32_t dx, int32_t dy)
{
    if (!surface->surface) return environment_move_invalid;

    enum environment_move_result result = environment_move_ok;

    // if (active_pointer) {
    //     if (active_pointer->above_surface) {
    //         environment_subsurface_t* above_surface = environment_subsurface_from_surface(active_pointer->above_surface);
    //         if (above_surface == surface && surface->pose) {
    //             active_pointer->surface_x = active_pointer->x - (dx + surface->pose->anchor_x);
    //             active_pointer->surface_y = active_pointer->y - (dy + surface->pose->anchor_y);
    //             if ((int)active_pointer->surface_x < 0 || (int)active_pointer->surface_y > (int)surface->pose->sprite[surface->mascot->LookingRight->value.i]->width / surface->env->scale
    //                 || (int)active_pointer->surface_y < 0 || (int)active_pointer->surface_y > (int)surface->pose->sprite[surface->mascot->LookingRight->value.i]->height / surface->env->scale) {
    //                 active_pointer->surface_x = 0;
    //                 active_pointer->surface_y = 0;
    //                 active_pointer->above_surface = NULL;
    //             }
    //         }
    //     }
    // }


    int32_t surface_anchor_x = 0, surface_anchor_y = 0;
    surface_anchor_x = surface->offset_x;
    surface_anchor_y = surface->offset_y;

    if (surface->pose) {
        surface_anchor_x += (surface->mascot->LookingRight->value.i ? -surface->width - surface->pose->anchor_x : surface->pose->anchor_x);
        surface_anchor_y += surface->pose->anchor_y;
    }

    surface_anchor_x = (float)surface_anchor_x / surface->env->scale;
    surface_anchor_y = (float)surface_anchor_y / surface->env->scale;

    wl_subsurface_set_position(surface->subsurface, dx + surface_anchor_x, dy + surface_anchor_y);

    surface->x = dx;
    surface->y = dy;

    surface->env->pending_commit = true;

    return result;
}

int32_t environment_screen_width(environment_t* env)
{
    return env->output.width / env->scale;
}

int32_t environment_screen_height(environment_t* env)
{
    return env->output.height / env->scale;
}

int32_t environment_workarea_width(environment_t* env)
{
    return env->width;
}

int32_t environment_workarea_height(environment_t* env)
{
    return env->height;
}

uint32_t environment_id(environment_t* env)
{
    return env->id;
}

const char* environment_name(environment_t* env)
{
    return env->xdg_output_done ? env->xdg_output_name : env->output.name;
}

const char* environment_desc(environment_t* env)
{
    return env->xdg_output_done ? env->xdg_output_desc : env->output.desc;
}

bool environment_logical_position(environment_t *env, int32_t *lx, int32_t *ly)
{
    if (!env->xdg_output_done) return false;
    *lx = env->lx;
    *ly = env->ly;
    return true;
}

bool environment_logical_size(environment_t *env, int32_t *lw, int32_t *lh)
{
    if (!env->xdg_output_done) return false;
    *lw = env->lwidth;
    *lh = env->lheight;
    return true;
}

enum environment_border_type environment_get_border_type(environment_t *env, int32_t x, int32_t y)
{
    if (!env->is_ready) return environment_border_type_invalid;

    y = yconv(env, y);

    int32_t ie_out_x = 0;
    int32_t ie_out_y = 0;
    enum environment_border_type ie_border_type = environment_try_ie_collision(env, x, y, x, y, &ie_out_x, &ie_out_y);

    // Detect type of border by coordinates using output size (width, height)
    if (y <= 0) return environment_border_type_ceiling;
    else if (x >= (int32_t)environment_workarea_width(env)|| x <= 0) return environment_border_type_wall;
    else if (y >= (int32_t)environment_workarea_height(env)) return environment_border_type_floor;
    else if (ie_border_type == environment_border_type_wall) return environment_border_type_wall;
    else if (ie_border_type == environment_border_type_ceiling) return environment_border_type_ceiling;
    else if (ie_border_type == environment_border_type_floor) return environment_border_type_floor;
    else return environment_border_type_none;
}


void environment_set_broadcast_input_enabled_listener(void(*listener)(bool))
{
    env_broadcast_input_enabled = listener;
}

void environment_set_mascot_by_coords_callback(struct mascot* (*callback)(environment_t*, int32_t, int32_t))
{
    mascot_by_coordinates = callback;
}

bool environment_migrate_subsurface(environment_subsurface_t* surface, environment_t* env)
{
    if (!surface) return false;
    if (!env) return false;

    if (surface->env == env) return true;

    const struct mascot_pose* pose = environment_subsurface_get_pose(surface);

    // First, unmap the surface
    environment_subsurface_unmap(surface);

    // Unlink subsurface from the old environment
    struct mascot* mascot = environment_subsurface_get_mascot(surface);
    if (mascot) {
        uint32_t mascot_index = list_find(surface->env->referenced_mascots, mascot);
        if (mascot_index != UINT32_MAX) {
            list_remove(surface->env->referenced_mascots, mascot_index);
        }
    }

    // Next we change our vision of the environment
    surface->env = env;

    // Link subsurface to the new environment
    if (mascot) {
        list_add(env->referenced_mascots, mascot);
    }

    // Get all user data from the surface
    struct wl_surface_data* userdata = wl_surface_get_user_data(surface->surface);

    if (surface->viewport) wp_viewport_destroy(surface->viewport);
    if (surface->fractional_scale) wp_fractional_scale_v1_destroy(surface->fractional_scale);
    wl_subsurface_destroy(surface->subsurface);
    wl_surface_destroy(surface->surface);

    // Create new surface and subsurface
    surface->surface = wl_compositor_create_surface(compositor);
    if (!surface->surface) ERROR("Failed to create surface!");

    surface->subsurface = wl_subcompositor_get_subsurface(subcompositor, surface->surface, env->root_surface->surface);
    if (!surface->subsurface) ERROR("Failed to create subsurface!");

    if (viewporter) {
        surface->viewport = wp_viewporter_get_viewport(viewporter, surface->surface);
        if (!surface->viewport) ERROR("Failed to create viewport!");
    }

    if (env->output.scale && !fractional_manager && !viewporter) {
        wl_surface_set_buffer_scale(surface->surface, env->output.scale);
    }

    wl_surface_set_data(surface->surface, userdata->role, surface, env);
    wl_surface_attach_callbacks(surface->surface, userdata->callbacks);

    free(userdata);

    // Map the surface again
    environment_subsurface_attach(surface, pose);

    return true;

    // Subsurface stays unmapped until new pose is set
}

void environment_disable_tablet_workarounds(bool value)
{
    disable_tablet_workarounds = value;
    if (disable_tablet_workarounds & why_tablet_v2_proximity_in_events_received_by_parent_question_mark) {
        WARN("Tablet v2 proximity_in events received by parent workaround is disabled");
        why_tablet_v2_proximity_in_events_received_by_parent_question_mark = false;
    }
}

const char* environment_get_backend_name()
{
    return "Wayland";
}


environment_buffer_factory_t* environment_buffer_factory_new()
{
    if (!compositor) ERROR("Failed to create buffer factory: wl_compositor is not available");
    if (!shm_manager) ERROR("Failed to create buffer factory: wl_shm is not available");
    environment_buffer_factory_t* factory = (environment_buffer_factory_t*)calloc(1, sizeof(environment_buffer_factory_t));
    if (!factory) ERROR("Failed to create buffer factory: Out of memory");

    factory->memfd = memfd_create("environment_factory_buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (factory->memfd < 0) {
        free(factory);
        WARN("Failed to create buffer factory: Failed to create memfd");
        return NULL;
    }

    return factory;
}

void environment_buffer_factory_destroy(environment_buffer_factory_t* factory)
{
    if (!factory) return;
    if (factory->pool) wl_shm_pool_destroy(factory->pool);
    close(factory->memfd);
    free(factory);
}

bool environment_buffer_factory_write(environment_buffer_factory_t* factory, const void* data, size_t size)
{
    if (!factory) return false;
    if (write(factory->memfd, data, size) != (ssize_t)size) {
        WARN("Failed to write data to buffer factory");
        return false;
    }
    factory->size += size;
    return true;
}

void environment_buffer_factory_done(environment_buffer_factory_t* factory)
{
    if (!factory) return;
    if (factory->done) return;

    factory->pool = wl_shm_create_pool(shm_manager, factory->memfd, factory->size);
    if (!factory->pool) ERROR("Failed to create buffer factory pool");

    factory->done = true;
}

environment_buffer_t* environment_buffer_factory_create_buffer(environment_buffer_factory_t* factory, int32_t width, int32_t height, uint32_t stride, uint32_t offset)
{
    if (!factory) return NULL;
    if (!factory->done) return NULL;

    environment_buffer_t* buffer = (environment_buffer_t*)calloc(1, sizeof(environment_buffer_t));
    if (!buffer) ERROR("Failed to create buffer: Out of memory");

    buffer->buffer = wl_shm_pool_create_buffer(factory->pool, offset, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    if (!buffer->buffer) {
        free(buffer);
        WARN("Failed to create buffer: Failed to create wl_buffer");
        return NULL;
    }

    return buffer;
}

void environment_buffer_add_to_input_region(environment_buffer_t* buffer, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (!buffer) return;
    if (!buffer->buffer) return;

    if (!buffer->region) {
        buffer->region = wl_compositor_create_region(compositor);
        if (!buffer->region) {
            WARN("Failed to add buffer to input region: Failed to create region");
            return;
        }
    }
    wl_region_add(buffer->region, x, y, width, height);
}

void environment_buffer_subtract_from_input_region(environment_buffer_t* buffer, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (!buffer) return;
    if (!buffer->buffer) return;

    if (!buffer->region) {
        buffer->region = wl_compositor_create_region(compositor);
        if (!buffer->region) {
            WARN("Failed to subtract buffer from input region: Failed to create region");
            return;
        }
    }
    wl_region_subtract(buffer->region, x, y, width, height);
}

void environment_buffer_destroy(environment_buffer_t* buffer)
{
    if (!buffer) return;
    if (buffer->region) wl_region_destroy(buffer->region);
    if (buffer->buffer) wl_buffer_destroy(buffer->buffer);
    free(buffer);
}
