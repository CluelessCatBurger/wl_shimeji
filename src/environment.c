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

#include <pthread.h>
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
#include "config.h"
#include "list.h"
#include <wayland-cursor.h>
#include "physics.h"
#include "protocol/server.h"

// Usefull macros
#define pthread_scoped_lock(name, mutex) __attribute__((cleanup(pthread_scope_unlock))) pthread_mutex_t *__scope_lock##name = mutex;\
    pthread_mutex_lock(__scope_lock##name);

void pthread_scope_unlock(pthread_mutex_t **lockptr) {
    if (!*lockptr) return;
    pthread_mutex_unlock(*lockptr);
    *lockptr = NULL;
}

// Workarounds section (SMH my head hyprland)
bool we_on_hyprland = false;

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
struct wp_alpha_modifier_v1* alpha_modifier_manager = NULL;

// unstable extensions
struct zxdg_output_manager_v1* xdg_output_manager = NULL;

void (*new_environment)(environment_t*) = NULL;
void (*rem_environment)(environment_t*) = NULL;
void (*orphaned_mascot)(struct mascot*) = NULL;
void (*env_broadcast_input_enabled)(bool) = NULL;
environment_t* (*lookup_environment_by_coords)(int32_t, int32_t) = NULL;

#define environment_by_global_coords(x,y) lookup_environment_by_coords ? lookup_environment_by_coords(x, y) : NULL

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

    struct {
        struct list* referenced_mascots;
        struct mascot_affordance_manager* affordances;
        mascot_prototype_store* prototype_store;
        pthread_mutex_t mutex;
    } mascot_manager;

    struct ie_object* ie;

    bool select_active;
    void (*select_callback)(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data);
    void* select_data;

    void* external_data;

    struct bounding_box global_geometry;
    struct bounding_box workarea_geometry;
    struct bounding_box advertised_geometry;

    struct list* neighbors; // Neighbored environments in contact with this one
    int32_t border_mask; // Mask for border type checks for mascot
    bool ceiling_aligned;
    bool floor_aligned;
    bool left_aligned;
    bool right_aligned;
};

struct environment_shm_pool {
    struct wl_shm_pool* pool;
};

struct environment_popup {
    struct wl_surface* surface;
    struct xdg_popup* popup;
    struct xdg_surface* xdg_surface;
    struct xdg_positioner* position;
    uint32_t xdg_serial;
    uint32_t input_serial;

    struct mascot* mascot;
    environment_t* environment;
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
    struct wp_alpha_modifier_surface_v1* alpha_modifier;
    const struct mascot_pose* pose;
    environment_t* env;
    struct mascot* mascot;
    bool is_grabbed;
    struct environment_pointer* drag_pointer;
    int32_t x, y;
    int32_t width, height;
    int32_t offset_x, offset_y;

    struct {
        float new_x, new_y;
        float prev_x, prev_y;
        float x, y;
    } interpolation_data;
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

    float scale_factor;
    struct {
        uint32_t x, y;
        uint32_t width, height;
    } input_region_desc; // In buffer coordinates
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

struct active_ie {
    bool is_active;
    struct bounding_box geometry;
};

struct active_ie active_ie = {
    .is_active = false
};

// Helper functions ---------------------------------------------------------

void environment_recalculate_advertised_geometry(environment_t* env);

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
    env->workarea_geometry.width = width;
    env->workarea_geometry.height = height;
    pthread_mutex_lock(&env->mascot_manager.mutex);
    for (uint32_t i = 0; i < list_size(env->mascot_manager.referenced_mascots); i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (mascot) {
            if (mascot->Y->value.i == 0 || mascot->Y->value.i == env->advertised_geometry.height) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
            }
        }
    }
    pthread_mutex_unlock(&env->mascot_manager.mutex);
    environment_recalculate_advertised_geometry(env);
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
    environment_t* env = (environment_t*)data;

    pthread_mutex_lock(&env->mascot_manager.mutex);
    for (uint32_t i = 0; i < list_size(env->mascot_manager.referenced_mascots); i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (mascot) {
            if (mascot->Y->value.i == 0 || mascot->Y->value.i == env->advertised_geometry.height) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
            }
        }
    }
    pthread_mutex_unlock(&env->mascot_manager.mutex);

    environment_recalculate_advertised_geometry(env);
    for (uint32_t i = 0; i < list_size(env->neighbors); i++) {
        environment_t* neighbor = list_get(env->neighbors, i);
        environment_recalculate_advertised_geometry(neighbor);
    }
    protocol_server_environment_changed(env);

    env->xdg_output_done = true;
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

    env_pointer->frame.enter_serial = serial;
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
            if (mascot && (config_get_allow_dragging_multihead() || config_get_unified_outputs())) {
                int32_t diffx, diffy;
                environment_global_coordinates_delta(env, new_env, &diffx, &diffy);
                int32_t newx = env_pointer->x, newy = env_pointer->y;

                mascot_moved(mascot, newx, yconv(new_env, newy));
                mascot_apply_environment_position_diff(mascot, diffx, diffy, DIFF_HORIZONTAL_MOVE | DIFF_VERTICAL_MOVE, new_env);
                mascot_environment_changed(mascot, new_env);
                env_pointer->frame.mask &= ~EVENT_FRAME_MOTIONS;
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
        env_pointer->enter_serial = env_pointer->frame.enter_serial;
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

        global_x = mascot->X->value.i + wl_fixed_to_int(x) + anchor_x / (config_get_mascot_scale() * env_surface->env->scale);
        global_y = yconv(env_surface->env, mascot->Y->value.i) + wl_fixed_to_int(y) + anchor_y / (config_get_mascot_scale() * env_surface->env->scale);
    }


    env_pointer->x = global_x;
    env_pointer->y = global_y;

    if (env_pointer->grabbed_subsurface) return;

    // Get hotspot
    int32_t scaled_x = wl_fixed_to_int(env_pointer->frame.surface_local_x), scaled_y = wl_fixed_to_int(env_pointer->frame.surface_local_y);
    environment_subsurface_scale_coordinates(env_surface, &scaled_x, &scaled_y);
    struct mascot_hotspot* hotspot = mascot_hotspot_by_pos(env_surface->mascot, scaled_x, scaled_y);
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

    int32_t scaled_x = env_pointer->surface_x, scaled_y = env_pointer->surface_y;
    environment_subsurface_scale_coordinates(env_surface, &scaled_x, &scaled_y);

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (!env->select_active && !env_pointer->grabbed_subsurface) {
            bool hotspot_press_result = mascot_hotspot_hold(mascot, scaled_x, scaled_y, pressed_button, false);
            if (!hotspot_press_result && pressed_button == mascot_hotspot_button_left) {
                mascot_drag_started(mascot, env_pointer);
            }
            if (!hotspot_press_result && pressed_button == mascot_hotspot_button_right) {
                protocol_server_announce_new_click(mascot, env_pointer->x, env_pointer->y);
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
                mascot_hotspot_hold(mascot, scaled_x, scaled_y, pressed_button, true);
                struct mascot_hotspot* hotspot = mascot_hotspot_by_pos(mascot, scaled_x, scaled_y);
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
        ERROR("Critical! No pointer in mascot_on_pointer_axis!");
    }

    environment_subsurface_t* env_surface = environment_subsurface_from_surface(env_pointer->above_surface);
    if (!env_surface) {
        WARN("Unexpected lack of env_subsurface while we are in mascot_on_pointer_axis!");
        return;
    }

    environment_t* env = environment_from_surface(env_pointer->above_surface);
    if (!env) {
        WARN("Unexpected lack of env while we are in mascot_on_pointer_axis!");
        return;
    }

    struct mascot * mascot = environment_subsurface_get_mascot(env_surface);
    if (!mascot) {
        WARN("Unexpected lack of mascot while we are in mascot_on_pointer_axis!");
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

    // if (env_pointer->grabbed_subsurface) {
    //     env_pointer->frame.mask |= EVENT_FRAME_BUTTONS;
    //     env_pointer->frame.buttons_released |= EVENT_FRAME_PRIMARY_BUTTON;
    // }

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

    // if (why_tablet_v2_proximity_in_events_received_by_parent_question_mark) {
    //     if (!env_pointer->grabbed_subsurface) {
    //         // Now our coordinates is in global space, we to map them back to surface space
    //         int32_t global_x = wl_fixed_to_int(x);
    //         int32_t global_y = wl_fixed_to_int(y);

    //         int32_t local_x;
    //         int32_t local_y;

    //         environment_t* env = environment_from_surface(env_pointer->above_surface);
    //         if (!env) return;

    //         struct environment_subsurface* env_subsurface = environment_subsurface_from_surface(env_pointer->above_surface);
    //         if (!env_subsurface) return;

    //         struct mascot* mascot = env_subsurface->mascot;
    //         if (!mascot) return;

    //         int32_t anchor_x = 0;
    //         int32_t anchor_y = 0;

    //         if (env_subsurface->pose) {
    //             anchor_x = env_subsurface->pose->anchor_x;
    //             anchor_y = env_subsurface->pose->anchor_y;
    //         }

    //         // Transform global coordinates to surface_local coordinates
    //         local_x = global_x - anchor_x * (config_get_mascot_scale() * env->scale) - mascot->X->value.i;
    //         local_y = global_y - anchor_y * (config_get_mascot_scale() * env->scale) - yconv(env, mascot->Y->value.i);

    //         env_pointer->frame.surface_local_x = wl_fixed_from_int(local_x);
    //         env_pointer->frame.surface_local_y = wl_fixed_from_int(local_y);
    //     } else {
    //         environment_t* env = environment_from_surface(env_pointer->above_surface);
    //         if (!env) return;
    //         if (env_pointer->above_surface == env_pointer->grabbed_subsurface->surface) {
    //             env_pointer->frame.mask |= EVENT_FRAME_SURFACE;
    //             env_pointer->frame.surface_changed = env->root_surface->surface;
    //             env_pointer->frame.enter_serial = env_pointer->enter_serial;
    //         }
    //     }
    // }

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
                // if (!why_tablet_v2_proximity_in_events_received_by_parent_question_mark && !disable_tablet_workarounds) {
                //     why_tablet_v2_proximity_in_events_received_by_parent_question_mark = true;
                //     WARN("WORKAROUND: zwp_tablet_v2 proximity_in events are received by parent in KDE, not by subsurface. Applying stupid workaround.");
                // }
                struct mascot* mascot = environment_mascot_by_coordinates(env, wl_fixed_to_int(env_pointer->frame.surface_local_x), wl_fixed_to_int(env_pointer->frame.surface_local_y));
                if (mascot) {
                    env_pointer->frame.surface_changed = mascot->subsurface->surface;
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
    env->xdg_output_done = false;
    env->lx = x;
    env->ly = y;
    env->global_geometry.x = x;
    env->global_geometry.y = y;
}

static void xdg_output_logical_size(void* data, struct zxdg_output_v1* xdg_output, int32_t width, int32_t height)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = false;
    env->lwidth = width;
    env->lheight = height;
    env->global_geometry.width = width;
    env->global_geometry.height = height;
}

static void xdg_output_name(void* data, struct zxdg_output_v1* xdg_output, const char* name)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = false;
    env->output.name = strdup(name);
}

static void xdg_output_description(void* data, struct zxdg_output_v1* xdg_output, const char* description)
{
    UNUSED(xdg_output);
    environment_t* env = data;
    env->xdg_output_done = false;
    env->output.desc = strdup(description);
}

static void xdg_output_done(void* data, struct zxdg_output_v1* xdg_output)
{
    UNUSED(xdg_output);
    environment_t* env = data;

    pthread_mutex_lock(&env->mascot_manager.mutex);
    for (uint32_t i = 0; i < list_size(env->mascot_manager.referenced_mascots); i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (mascot) {
            if (mascot->Y->value.i == 0 || mascot->Y->value.i == env->advertised_geometry.height) {
                mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
            }
        }
    }
    pthread_mutex_unlock(&env->mascot_manager.mutex);

    environment_recalculate_advertised_geometry(env);
    for (uint32_t i = 0; i < list_size(env->neighbors); i++) {
        environment_t* neighbor = list_get(env->neighbors, i);
        environment_recalculate_advertised_geometry(neighbor);
    }
    protocol_server_environment_changed(env);

    env->xdg_output_done = true;
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
        pthread_mutexattr_t attrs;
        pthread_mutexattr_init(&attrs);
        pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_RECURSIVE);
        env->id = wl_refcounter++;
        env->output.output = output;
        env->output.id = display_id++;
        env->mascot_manager.referenced_mascots = list_init(256);
        env->neighbors = list_init(4);
        pthread_mutex_init(&env->mascot_manager.mutex, &attrs);
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

static void handle_alpha_modifier_v1(void* data, uint32_t id, uint32_t version)
{
    UNUSED(data);
    alpha_modifier_manager = wl_registry_bind(registry, id, &wp_alpha_modifier_v1_interface, version);
    INFO("Binded wp_alpha_modifier_v1 global of ver %u", version);
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
    } else if (!strcmp(iface_name, wp_alpha_modifier_v1_interface.name)) {
        handle_alpha_modifier_v1(data, id, version);
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
    void(*orph_listener)(struct mascot*)
)
{

    const char* session = getenv("XDG_CURRENT_DESKTOP");
    if (session) {
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
    pthread_mutex_lock(&env->mascot_manager.mutex);
    for (uint32_t i = 0; i < list_size(env->mascot_manager.referenced_mascots); i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (mascot) {
            if (orphaned_mascot) {
                orphaned_mascot(mascot);
            } else {
                WARN("<Mascot:%s:%u> Can not find new environment for orphaned mascot, dismissing", mascot->prototype->name, mascot->id);
                mascot_attach_affordance_manager(mascot, NULL);
                mascot_unlink(mascot);
            }
            list_remove(env->mascot_manager.referenced_mascots, i);
        }
    }
    list_free(env->mascot_manager.referenced_mascots);
    pthread_mutex_unlock(&env->mascot_manager.mutex);

    protocol_server_environment_widthdraw(env);

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
    wl_display_flush(display);
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

    if (alpha_modifier_manager) {
        subsurface->alpha_modifier = wp_alpha_modifier_v1_get_surface(alpha_modifier_manager, subsurface->surface);
        wp_alpha_modifier_surface_v1_set_multiplier(subsurface->alpha_modifier, (uint32_t)((double)config_get_opacity() * (double)UINT32_MAX));
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
    if (surface->alpha_modifier) {
        wp_alpha_modifier_surface_v1_destroy(surface->alpha_modifier);
    }

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
    if (!surface->drag_pointer && config_get_dragging()) {
        environment_buffer_scale_input_region(sprite->buffer, (config_get_mascot_scale() * surface->env->scale));
        wl_surface_set_input_region(surface->surface, sprite->buffer->region);
    } else {
        wl_surface_set_input_region(surface->surface, empty_region);
    }

    if (!surface->pose) {
        wl_subsurface_place_below(surface->subsurface, surface->env->root_environment_subsurface->surface);
        if (surface->mascot) {
            environment_subsurface_set_position(surface, surface->mascot->X->value.i, yconv(surface->env, surface->mascot->Y->value.i));
            environment_subsurface_reset_interpolation(surface);
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
            sprite->width / (config_get_mascot_scale() * surface->env->scale),
            sprite->height / (config_get_mascot_scale() * surface->env->scale)
        );
        wl_surface_commit(surface->surface);
    } else {
        wl_surface_set_buffer_scale(surface->surface, surface->env->scale);
    }

    if (surface->alpha_modifier) {
        wp_alpha_modifier_surface_v1_set_multiplier(surface->alpha_modifier, (uint32_t)((double)config_get_opacity() * (double)UINT32_MAX));
    }

    environment_subsurface_set_position(surface, surface->mascot->X->value.i, yconv(surface->env, surface->mascot->Y->value.i));

    surface->env->pending_commit = true;
}

enum environment_move_result environment_subsurface_move(environment_subsurface_t* surface, int32_t dx, int32_t dy, bool use_callback, bool use_interpolation)
{
    if (!surface->surface) return environment_move_invalid;
    if (!config_get_interpolation_framerate()) use_interpolation = false;

    enum environment_move_result result = environment_move_ok;

    int32_t proposed_x = dx, proposed_y = dy;
    int32_t collision = 0;

    int current_x = dx;
    int current_y = yconv(surface->env, dy);
    if (surface->mascot) {
        current_x = surface->mascot->X->value.i;
        current_y = yconv(surface->env, surface->mascot->Y->value.i);
    }

    // Check collisions for environment
    collision = check_movement_collision(
        &surface->env->workarea_geometry,
        current_x, current_y,
        dx, yconv(surface->env, dy),
        BORDER_TYPE_NONE,
        &proposed_x, &proposed_y
    );

    if ((MOVE_OOB(collision) || is_outside(&surface->env->workarea_geometry, current_x, current_y)) && (config_get_allow_throwing_multihead() || config_get_unified_outputs())) {
        int32_t global_x = dx, global_y = yconv(surface->env, dy);
        environment_to_global_coordinates(surface->env, &global_x, &global_y);
        environment_t* new_env = environment_by_global_coords(global_x, global_y);
        if (new_env == surface->env) {
            struct bounding_box *gbbx = &surface->env->global_geometry;
            struct bounding_box *lbbx = &surface->env->workarea_geometry;

            if (dy > gbbx->height) {
                gbbx->y -= gbbx->height - lbbx->height;
                gbbx->height -= gbbx->height - lbbx->height;
            } else if (dy < 0) {
                gbbx->y += gbbx->height - lbbx->height;
                gbbx->height -= gbbx->height - lbbx->height;
            }

            if (dx > gbbx->width) {
                gbbx->x -= gbbx->width - lbbx->width;
                gbbx->width -= gbbx->width - lbbx->width;
            } else if (dx < 0) {
                gbbx->x += gbbx->width - lbbx->width;
                gbbx->width -= gbbx->width - lbbx->width;
            }

            mascot_moved(surface->mascot, proposed_x, yconv(surface->env, proposed_y));
            return environment_move_ok;
        }
        else if (new_env) {
            int32_t diff_x, diff_y;
            environment_global_coordinates_delta(surface->env, new_env, &diff_x, &diff_y);
            proposed_x = dx;
            proposed_y = dy;

            int32_t move_flags = DIFF_HORIZONTAL_MOVE | DIFF_VERTICAL_MOVE;
            // if (!(collision & (BORDER_TYPE_CEILING | BORDER_TYPE_FLOOR))) {
            //     move_flags |= DIFF_VERTICAL_MOVE;
            // }
            // if (BORDER_IS_WALL(collision)) {
            //     move_flags |= DIFF_HORIZONTAL_MOVE;
            // }

            mascot_moved(surface->mascot, proposed_x, proposed_y);
            mascot_apply_environment_position_diff(surface->mascot, diff_x, diff_y, move_flags, new_env);
            mascot_environment_changed(surface->mascot, new_env);

            if (is_outside(&new_env->workarea_geometry, surface->mascot->X->value.i, surface->mascot->Y->value.i)) {
                if (surface->mascot->X->value.i < new_env->workarea_geometry.x) {
                    surface->mascot->X->value.i = new_env->workarea_geometry.x;
                } else if (surface->mascot->X->value.i > new_env->workarea_geometry.x + new_env->workarea_geometry.width) {
                    surface->mascot->X->value.i = new_env->workarea_geometry.x + new_env->workarea_geometry.width;
                }
                if (surface->mascot->Y->value.i < new_env->workarea_geometry.y) {
                    surface->mascot->Y->value.i = new_env->workarea_geometry.y;
                } else if (surface->mascot->Y->value.i > new_env->workarea_geometry.y + new_env->workarea_geometry.height) {
                    surface->mascot->Y->value.i = new_env->workarea_geometry.y + new_env->workarea_geometry.height;
                }
            }

            return environment_move_ok;
        }
    }

    if (COLLIDED(collision) && !environment_neighbor_border(surface->env, proposed_x, proposed_y) && surface->mascot->state != mascot_state_jump) {
        dx = proposed_x;
        dy = yconv(surface->env, proposed_y);
        result = environment_move_clamped;
    }

    // Now try collision with IE
    if (environment_ie_is_active() && !COLLIDED(collision)){
        int32_t proposed_x = dx;
        int32_t proposed_y = dy;

        struct bounding_box bb = environment_get_active_ie(surface->env);

        collision = check_movement_collision(&bb, current_x, current_y, dx, yconv(surface->env, dy), BORDER_TYPE_FLOOR, &proposed_x, &proposed_y);

        if (COLLIDED(collision)) {
            mascot_moved(surface->mascot, proposed_x, yconv(surface->env, proposed_y));
            return environment_move_clamped;
        }
    }

    dy = yconv(surface->env, dy);

    if (!use_interpolation) {
        if (environment_subsurface_set_position(surface, dx, dy) == environment_move_invalid) {
            return environment_move_invalid;
        }
        environment_subsurface_reset_interpolation(surface);
    }


    surface->interpolation_data.prev_x = surface->interpolation_data.x;
    surface->interpolation_data.prev_y = surface->interpolation_data.y;

    surface->interpolation_data.new_x = dx;
    surface->interpolation_data.new_y = dy;

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

    if (!config_get_unified_outputs() && config_get_allow_dragging_multihead()) {
        int32_t x = surface->mascot->X->value.i;
        int32_t y = yconv(surface->env, surface->mascot->Y->value.i);
        int32_t global_x = x, global_y = y;
        if (is_outside(&surface->env->workarea_geometry, global_x, global_y)) {
            environment_to_global_coordinates(surface->env, &global_x, &global_y);
            environment_t* new_env = environment_by_global_coords(global_x, global_y);
            if (new_env) {
                int32_t diff_x, diff_y;
                environment_global_coordinates_delta(surface->env, new_env, &diff_x, &diff_y);

                mascot_moved(surface->mascot, x, yconv(surface->env, y));
                mascot_apply_environment_position_diff(surface->mascot, diff_x, diff_y, DIFF_HORIZONTAL_MOVE | DIFF_VERTICAL_MOVE, new_env);
                mascot_environment_changed(surface->mascot, new_env);
            }
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

    return pointer->grabbed_subsurface ? pointer->x : pointer->public_x - env->global_geometry.x;
}

int32_t environment_cursor_y(struct mascot* mascot, environment_t* env) {
    UNUSED(env);
    environment_pointer_t* pointer = mascot ? mascot->subsurface->drag_pointer : active_pointer;
    if (!pointer) pointer = active_pointer;
    if (!pointer) return 0;

    return pointer->grabbed_subsurface ? pointer->y : pointer->public_y - env->global_geometry.y;
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
    return config_get_mascot_scale() * env->scale;
}

void environment_subsurface_associate_mascot(environment_subsurface_t* surface, struct mascot* mascot_ptr)
{
    if (!surface) return;
    surface->mascot = mascot_ptr;
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
    if (env->select_active) environment_pointer_apply_cursor(active_pointer, mascot_hotspot_cursor_crosshair);
    environment_set_input_state(env, env->select_active);
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
    if (!surface) return NULL;
    return surface->mascot;
}

const struct mascot_pose* environment_subsurface_get_pose(environment_subsurface_t* surface)
{
    if (!surface) return NULL;
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

bool environment_ask_close(environment_t *environment)
{
    if (!environment) return false;
    if (!environment->is_ready) return false;

    environment_wants_to_close_callback(environment);
    return true;
}

environment_shm_pool_t* environment_import_shm_pool(int32_t fd, uint32_t size)
{
    if (fd < 0 || size == 0) return NULL;

    void* shared_pool = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (!shared_pool) {
        WARN("Cannot import shared memory pool requested by client");
        return NULL;
    }
    munmap(shared_pool, size);

    environment_shm_pool_t* pool = calloc(1, sizeof(environment_shm_pool_t));
    if (!pool) return NULL;

    pool->pool = wl_shm_create_pool(shm_manager, fd, size);
    close(fd);

    return pool;
}

environment_buffer_t* environment_shm_pool_create_buffer(
    environment_shm_pool_t* pool,
    uint32_t offset,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    uint32_t format
)
{
    if (!pool) return NULL;

    environment_buffer_t* buffer = calloc(1, sizeof(environment_buffer_t));
    if (!buffer) return NULL;

    buffer->buffer = wl_shm_pool_create_buffer(pool->pool, offset, width, height, stride, format);
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;

    return buffer;
}

void environment_shm_pool_destroy(environment_shm_pool_t* pool)
{
    if (!pool) return;

    wl_shm_pool_destroy(pool->pool);
    free(pool);
}

environment_popup_t* environment_popup_create(environment_t* environment, struct mascot* mascot, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!environment || !mascot) return NULL;

    environment_popup_t* popup = calloc(1, sizeof(environment_popup_t));
    if (!popup) return NULL;

    popup->environment = environment;
    popup->mascot = mascot;

    popup->surface = wl_compositor_create_surface(compositor);
    popup->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, popup->surface);
    popup->position = xdg_wm_base_create_positioner(xdg_wm_base);

    xdg_positioner_set_anchor_rect(popup->position, x, y, 1, 1);
    xdg_positioner_set_size(popup->position, width, height);

    popup->popup = xdg_surface_get_popup(popup->xdg_surface, NULL, popup->position);
    zwlr_layer_surface_v1_get_popup(environment->root_surface->layer_surface, popup->popup);

    struct environment_callbacks* callbacks = calloc(1, sizeof(struct environment_callbacks));
    wl_surface_attach_callbacks(popup->surface, callbacks);

    return popup;
}

void environment_popup_commit(environment_popup_t* popup)
{
    if (!popup) return;

    wl_surface_commit(popup->surface);
}


#endif

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

    int32_t surface_anchor_x = 0, surface_anchor_y = 0;
    surface_anchor_x = surface->offset_x;
    surface_anchor_y = surface->offset_y;

    if (surface->pose) {
        surface_anchor_x += (surface->mascot->LookingRight->value.i ? -surface->width - surface->pose->anchor_x : surface->pose->anchor_x);
        surface_anchor_y += surface->pose->anchor_y;
    }

    surface_anchor_x = (float)surface_anchor_x / (config_get_mascot_scale() * surface->env->scale);
    surface_anchor_y = (float)surface_anchor_y / (config_get_mascot_scale() * surface->env->scale);

    wl_subsurface_set_position(surface->subsurface, dx + surface_anchor_x, dy + surface_anchor_y);

    surface->x = dx;
    surface->y = dy;

    // if (is_outside(&surface->env->geometry, dx, dy) && surface->is_grabbed) {
    //     result = environment_move_out_of_bounds;
    // }

    surface->env->pending_commit = true;

    return result;
}

int32_t environment_screen_width(environment_t* env)
{
    return env->output.width / (config_get_mascot_scale() * env->scale);
}

int32_t environment_screen_height(environment_t* env)
{
    return env->output.height / (config_get_mascot_scale() * env->scale);
}

int32_t environment_workarea_width(environment_t* env)
{
    return env->advertised_geometry.width ? env->advertised_geometry.width : (int32_t)env->width;
}

int32_t environment_workarea_height(environment_t* env)
{
    return env->advertised_geometry.height ? env->advertised_geometry.height : (int32_t)env->workarea_geometry.height;

}

int32_t environment_workarea_left(environment_t* env)
{
    return env->advertised_geometry.x;
}

int32_t environment_workarea_right(environment_t* env)
{
    return env->advertised_geometry.x + env->advertised_geometry.width;
}

int32_t environment_workarea_top(environment_t* env)
{
    return env->advertised_geometry.y;
}

int32_t environment_workarea_bottom(environment_t* env)
{
    return env->advertised_geometry.y + env->advertised_geometry.height;
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

enum environment_border_type environment_get_border_type_rect(environment_t *env, int32_t x, int32_t y, struct bounding_box* rect, int32_t border_mask)
{
    int32_t border_type = BORDER_TYPE(check_collision_at(rect, x, y, 0));
    int32_t masked_border = APPLY_MASK(border_type, border_mask);

    if (masked_border & BORDER_TYPE_CEILING) return (rect->type == INNER_COLLISION ? environment_border_type_ceiling : environment_border_type_floor);
    if (BORDER_IS_WALL(masked_border)) return environment_border_type_wall;
    if (masked_border & BORDER_TYPE_FLOOR) return (rect->type == INNER_COLLISION ? environment_border_type_floor : environment_border_type_ceiling);

    if (environment_neighbor_border(env, x, y)) return environment_border_type_none;

    if (border_type & BORDER_TYPE_CEILING) return (rect->type == INNER_COLLISION ? environment_border_type_ceiling : environment_border_type_floor);
    if (BORDER_IS_WALL(border_type)) return environment_border_type_wall;
    if (border_type & BORDER_TYPE_FLOOR) return (rect->type == INNER_COLLISION ? environment_border_type_floor : environment_border_type_ceiling);

    return environment_border_type_none;
}

enum environment_border_type environment_get_border_type(environment_t *env, int32_t x, int32_t y)
{
    if (!env->is_ready) return environment_border_type_invalid;

    y = yconv(env, y);

    return environment_get_border_type_rect(env, x, y, &env->workarea_geometry, env->border_mask);
}


void environment_set_broadcast_input_enabled_listener(void(*listener)(bool))
{
    env_broadcast_input_enabled = listener;
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
        mascot_announce_affordance(mascot, NULL);
        uint32_t mascot_index = list_find(surface->env->mascot_manager.referenced_mascots, mascot);
        if (mascot_index != UINT32_MAX) {
            list_remove(surface->env->mascot_manager.referenced_mascots, mascot_index);
        }
    }

    mascot_attach_affordance_manager(mascot, NULL);

    // Next we change our vision of the environment
    surface->env = env;

    mascot_attach_affordance_manager(mascot, env->mascot_manager.affordances);

    // Get all user data from the surface
    struct wl_surface_data* userdata = wl_surface_get_user_data(surface->surface);

    if (surface->alpha_modifier) wp_alpha_modifier_surface_v1_destroy(surface->alpha_modifier);
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

    if (alpha_modifier_manager) {
        surface->alpha_modifier = wp_alpha_modifier_v1_get_surface(alpha_modifier_manager, surface->surface);
        if (!surface->alpha_modifier) ERROR("Failed to create alpha modifier!");
    }

    if (env->output.scale && !fractional_manager && !viewporter) {
        wl_surface_set_buffer_scale(surface->surface, env->output.scale);
    }

    wl_surface_set_data(surface->surface, userdata->role, surface, env);
    wl_surface_attach_callbacks(surface->surface, userdata->callbacks);

    free(userdata);

    // Link subsurface to the new environment
    // pthread_mutex_lock(&env->mascot_manager.mutex);
    if (mascot) {
        list_add(env->mascot_manager.referenced_mascots, mascot);
    }

    // Map the surface again
    environment_subsurface_attach(surface, pose);
    // pthread_mutex_unlock(&env->mascot_manager.mutex);

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
        buffer->scale_factor = 1;
    }
    wl_region_add(buffer->region, x, y, width, height);
    buffer->input_region_desc.x = x;
    buffer->input_region_desc.y = y;
    buffer->input_region_desc.width = width;
    buffer->input_region_desc.height = height;

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
        buffer->scale_factor = 1;
    }
    wl_region_subtract(buffer->region, x, y, width, height);
}

void environment_buffer_scale_input_region(environment_buffer_t* buffer, float scale)
{
    if (!buffer) return;
    if (!buffer->buffer) return;
    if (!buffer->region) return;

    if (buffer->scale_factor == scale) return;

    wl_region_destroy(buffer->region);
    buffer->region = wl_compositor_create_region(compositor);
    if (!buffer->region) {
        WARN("Failed to scale input region: Failed to create region");
        return;
    }

    wl_region_add(buffer->region, buffer->input_region_desc.x/scale, buffer->input_region_desc.y/scale, buffer->input_region_desc.width/scale, buffer->input_region_desc.height/scale);

    buffer->scale_factor = scale;
}

void environment_buffer_destroy(environment_buffer_t* buffer)
{
    if (!buffer) return;
    if (buffer->region) wl_region_destroy(buffer->region);
    if (buffer->buffer) wl_buffer_destroy(buffer->buffer);
    free(buffer);
}

uint64_t environment_interpolate(environment_t *env)
{
    if (!env) return 0;
    if (!env->is_ready) return 0;

    float framerate = config_get_interpolation_framerate();
    if (framerate < 0) framerate = (env->output.refresh / 1000.0);
    if (framerate < 1.0) return 0;

    float progress_fraction = framerate / 25.0;
    // Iterate through references mascots
    pthread_mutex_lock(&env->mascot_manager.mutex);
    for (uint32_t i = 0, c = list_count(env->mascot_manager.referenced_mascots); i < list_size(env->mascot_manager.referenced_mascots) && c > 0; i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (mascot) {
            c--;
            if (mascot->subsurface) {
                if (mascot->subsurface->interpolation_data.x == mascot->subsurface->interpolation_data.new_x && mascot->subsurface->interpolation_data.y == mascot->subsurface->interpolation_data.new_y) {
                    continue;
                }

                if (mascot->subsurface->is_grabbed) continue;
                float delta_x = mascot->subsurface->interpolation_data.new_x - mascot->subsurface->interpolation_data.prev_x;
                float delta_y = mascot->subsurface->interpolation_data.new_y - mascot->subsurface->interpolation_data.prev_y;
                float chx = delta_x / progress_fraction;
                float chy = delta_y / progress_fraction;
                float new_x = mascot->subsurface->interpolation_data.x + chx;
                float new_y = mascot->subsurface->interpolation_data.y + chy;

                // If we reached the target position, stop moving on this axis
                if (delta_x > 0 && new_x > mascot->subsurface->interpolation_data.new_x) new_x = mascot->subsurface->interpolation_data.new_x;
                if (delta_x < 0 && new_x < mascot->subsurface->interpolation_data.new_x) new_x = mascot->subsurface->interpolation_data.new_x;
                if (delta_y > 0 && new_y > mascot->subsurface->interpolation_data.new_y) new_y = mascot->subsurface->interpolation_data.new_y;
                if (delta_y < 0 && new_y < mascot->subsurface->interpolation_data.new_y) new_y = mascot->subsurface->interpolation_data.new_y;


                environment_subsurface_set_position(mascot->subsurface, round(new_x), round(new_y));
                mascot->subsurface->interpolation_data.x = new_x;
                mascot->subsurface->interpolation_data.y = new_y;
            }
        }
    }
    pthread_mutex_unlock(&env->mascot_manager.mutex);
    // Usecs to sleep
    return 1000000 / (env->output.refresh / 1000.0);
}

void environment_set_user_data(environment_t* env, void* data)
{
    if (!env) return;
    env->external_data = data;
}

void* environment_get_user_data(environment_t* env)
{
    if (!env) return NULL;
    return env->external_data;
}

void environment_subsurface_reset_interpolation(environment_subsurface_t* subsurface)
{
    if (!subsurface) return;
    subsurface->interpolation_data.new_x = subsurface->x;
    subsurface->interpolation_data.new_y = subsurface->y;
    subsurface->interpolation_data.prev_x = subsurface->x;
    subsurface->interpolation_data.prev_y = subsurface->y;
    subsurface->interpolation_data.x = subsurface->x;
    subsurface->interpolation_data.y = subsurface->y;
}

void environment_subsurface_scale_coordinates(environment_subsurface_t* surface, int32_t* x, int32_t* y)
{
    if (!surface) return;
    if (!x || !y) return;
    *x = *x * (config_get_mascot_scale() * surface->env->scale);
    *y = *y * (config_get_mascot_scale() * surface->env->scale);
}

uint32_t environment_tick(environment_t* environment, uint32_t tick)
{
    if (!environment) return 0;
    if (!environment->is_ready) return 0;
    pthread_mutex_lock(&environment->mascot_manager.mutex);
    struct list* mascots = environment->mascot_manager.referenced_mascots;
    struct mascot_tick_return result = {};
    for (size_t i = 0, c = 0; i < list_size(mascots) && c < list_count(mascots); i++) {
        struct mascot* mascot = list_get(mascots, i);
        if (!mascot) continue;
        ++c;
        enum mascot_tick_result tick_status = mascot_tick(mascot, tick, &result);
        if (tick_status == mascot_tick_dispose) {
            mascot_announce_affordance(mascot, NULL);
            mascot_attach_affordance_manager(mascot, NULL);
            mascot_unlink(mascot);
            list_remove(mascots, i);
        } else if (tick_status == mascot_tick_error) {
            mascot_announce_affordance(mascot, NULL);
            mascot_attach_affordance_manager(mascot, NULL);
            mascot_unlink(mascot);
            list_remove(mascots, i);
        }
        if (result.events_count) {
            for (size_t j = 0; j < result.events_count; j++) {
                enum mascot_tick_result event_type = result.events[j].event_type;
                if (event_type == mascot_tick_clone) {
                    struct mascot* clone = result.events[j].event.mascot;
                    if (clone) {
                        list_add(mascots, clone);
                        mascot_attach_affordance_manager(clone, environment->mascot_manager.affordances);
                        mascot_link(clone);
                        protocol_server_environment_emit_mascot(environment, clone);
                    }
                }
            }
        }
        if (tick_status == mascot_tick_reenter) {
            i--;
            c--;
        }
    }
    pthread_mutex_unlock(&environment->mascot_manager.mutex);
    return list_count(mascots);
}

void environment_remove_mascot(environment_t* environment, struct mascot* mascot)
{
    if (!environment) return;
    if (!mascot) return;
    pthread_mutex_lock(&environment->mascot_manager.mutex);

    int res = ACTION_SET_ACTION_TRANSIENT;

    if (mascot->prototype->dismiss_action && config_get_allow_dismiss_animations()) {
        struct mascot_action_reference dismiss = {
            .action = (struct mascot_action*)mascot->prototype->dismiss_action,
        };
        res = mascot_set_action(mascot, &dismiss, false, 0);
    }

    if (res != ACTION_SET_RESULT_OK) {
        uint32_t index = list_find(environment->mascot_manager.referenced_mascots, mascot);
        if (index != UINT32_MAX) {
            list_remove(environment->mascot_manager.referenced_mascots, index);
        }
        mascot_attach_affordance_manager(mascot, NULL);
        mascot_unlink(mascot);
    }
    pthread_mutex_unlock(&environment->mascot_manager.mutex);
}

void environment_set_prototype_store(environment_t* environment, mascot_prototype_store* store)
{
    if (!environment) return;
    if (!store) return;
    environment->mascot_manager.prototype_store = store;
}

uint32_t environment_mascot_count(environment_t* environment)
{
    if (!environment) return 0;
    return list_count(environment->mascot_manager.referenced_mascots);
}

void environment_summon_mascot(
    environment_t* environment, struct mascot_prototype* prototype,
    int32_t x, int32_t y, void(*callback)(struct mascot*, void*), void* data
)
{
    if (!environment) return;
    if (!prototype) return;
    struct mascot* mascot = mascot_new(prototype, NULL, 0, 0, x, y, 2.0, 0.05, 0.1, false, environment);
    if (!mascot){
        if (callback) callback(mascot, data);
        return;
    }

    struct list *mascots = environment->mascot_manager.referenced_mascots;
    pthread_mutex_lock(&environment->mascot_manager.mutex);
    list_add(mascots, mascot);
    mascot_attach_affordance_manager(mascot, environment->mascot_manager.affordances);
    mascot_link(mascot);
    pthread_mutex_unlock(&environment->mascot_manager.mutex);

    protocol_server_environment_emit_mascot(environment, mascot);

    if (callback) callback(mascot, data);
}

void environment_set_global_coordinates_searcher(
    environment_t* (environment_by_global_coordinates)(int32_t x, int32_t y)
)
{
    lookup_environment_by_coords = environment_by_global_coordinates;
}

void environment_global_coordinates_delta(
    environment_t* environment_a,
    environment_t* environment_b,
    int32_t* dx, int32_t* dy
)
{
    if (!environment_a || !environment_b) return;
    if (!dx || !dy) return;

    // Calculate difference between two environments in global coordinates
    // This is useful for calculating the new position of a mascot when moving it between environments

    int32_t dx_ = environment_a->lx - environment_b->lx;
    int32_t dy_ = environment_a->ly - environment_b->ly;

    *dx = dx_;
    *dy = dy_;
}

struct mascot* environment_mascot_by_coordinates(environment_t* environment, int32_t x, int32_t y)
{
    if (!environment) return NULL;
    if (!environment->is_ready) return NULL;
    if (!environment->mascot_manager.referenced_mascots) return NULL;

    struct list* mascots = environment->mascot_manager.referenced_mascots;
    struct mascot* mascot = NULL;
    uint32_t mascot_score = 0;
    pthread_mutex_lock(&environment->mascot_manager.mutex);
    for (size_t i = 0, c = 0; i < list_size(mascots) && c < list_count(mascots); i++) {
        struct mascot* mascot_ = list_get(mascots, i);
        if (!mascot_) continue;
        c++;
        if (mascot_->dragged) continue;
        if (mascot_->subsurface) {
            struct bounding_box hitbox = {INNER_COLLISION,-1,-1,-1,-1};
            if (mascot_->subsurface->pose) {
                int surface_anchor_x = 0;
                int surface_anchor_y = 0;
                if (mascot_->subsurface->pose) {
                    surface_anchor_x += (mascot_->subsurface->mascot->LookingRight->value.i ? -mascot_->subsurface->width - mascot_->subsurface->pose->anchor_x : mascot_->subsurface->pose->anchor_x);
                    surface_anchor_y += mascot_->subsurface->pose->anchor_y;
                }

                surface_anchor_x = (float)surface_anchor_x / (config_get_mascot_scale() * mascot_->subsurface->env->scale);
                surface_anchor_y = (float)surface_anchor_y / (config_get_mascot_scale() * mascot_->subsurface->env->scale);

                struct mascot_sprite* sprite = mascot_->subsurface->pose->sprite[mascot_->LookingRight->value.i];
                hitbox.x = mascot_->subsurface->x + mascot_->subsurface->pose->anchor_x;
                hitbox.y = mascot_->subsurface->y + mascot_->subsurface->pose->anchor_y;
                hitbox.width = sprite->width;
                hitbox.height = sprite->height;
            }
            INFO("Checking hitbox: %d %d %d %d, pointer at (%d, %d)", hitbox.x, hitbox.y, hitbox.width, hitbox.height, x, y);
            if (is_inside(&hitbox, x, y) || check_collision_at(&hitbox, x, y, 0)) {
                INFO("Mascot found by coordinates: %p %d", mascot_, mascot_score);
                if (!mascot_->dragged_tick || mascot_->dragged_tick > mascot_score) {
                    mascot = mascot_;
                    mascot_score = mascot_->dragged_tick;
                }
            }
            // if (x >= mascot_->subsurface->x && x <= mascot_->subsurface->x + mascot_->subsurface->width) {

            // }
        }
    }
    pthread_mutex_unlock(&environment->mascot_manager.mutex);
    return mascot;
}

struct mascot* environment_mascot_by_id(environment_t* environment, uint32_t id)
{
    if (!environment) return NULL;
    if (!environment->is_ready) return NULL;
    if (!environment->mascot_manager.referenced_mascots) return NULL;

    struct list* mascots = environment->mascot_manager.referenced_mascots;
    struct mascot* mascot = NULL;
    pthread_mutex_lock(&environment->mascot_manager.mutex);
    for (size_t i = 0; i < list_size(mascots); i++) {
        struct mascot* mascot_ = list_get(mascots, i);
        if (!mascot_) continue;
        if (mascot_->id == id) {
            mascot = mascot_;
            break;
        }
    }
    pthread_mutex_unlock(&environment->mascot_manager.mutex);
    return mascot;
}

struct list* environment_mascot_list(environment_t* environment, pthread_mutex_t** list_mutex)
{
    if (!environment) return NULL;

    if (list_mutex) *list_mutex = &environment->mascot_manager.mutex;
    return environment->mascot_manager.referenced_mascots;
}

struct bounding_box* environment_global_geometry(environment_t* environment)
{
    if (!environment) return NULL;
    return &environment->global_geometry;
}

struct bounding_box* environment_local_geometry(environment_t* environment)
{
    if (!environment) return NULL;
    return &environment->workarea_geometry;
}

void environment_to_global_coordinates(
    environment_t* environment,
    int32_t* x, int32_t* y
)
{
    if (!environment) return;
    if (!x || !y) return;
    *x += environment->lx;
    *y += environment->ly;
}

void environment_recalculate_advertised_geometry(environment_t* env)
{
    if (!env) return;
    if (!env->is_ready) return;

    // Initialize variables to store the maximum dimensions and offsets
    int32_t max_left_offset = 0, max_right_offset = 0, max_top_offset = 0, max_bottom_offset = 0;
    int32_t max_width = env->workarea_geometry.width, max_height = env->workarea_geometry.height;

    int32_t new_border_mask = 0;
    bool ceiling_aligned = false, floor_aligned = false, left_aligned = false, right_aligned = false;

    // Iterate through the neighbors to find the maximum dimensions and offsets
    for (size_t i = 0; i < list_size(env->neighbors); i++) {
        environment_t* neighbor = list_get(env->neighbors, i);
        if (!neighbor) continue;
        if (!neighbor->is_ready) continue;

        // Check if the neighbor touches the left border
        if (bounding_box_touches(&env->global_geometry, &neighbor->global_geometry, BORDER_TYPE_RIGHT | BORDER_TYPE_CEILING | BORDER_TYPE_FLOOR)) {
            int32_t left_offset = env->global_geometry.x - neighbor->global_geometry.x;
            if (left_offset > max_left_offset) {
                max_left_offset = left_offset;
                max_width = neighbor->global_geometry.width + env->global_geometry.width;
            }
            new_border_mask |= BORDER_TYPE_LEFT;
            if (neighbor->global_geometry.y == env->global_geometry.y) {
                ceiling_aligned = true;
            }
            if (neighbor->global_geometry.y + neighbor->global_geometry.height == env->global_geometry.y + env->global_geometry.height) {
                floor_aligned = true;
            }
            continue;
        }

        // Check if the neighbor touches the right border
        if (bounding_box_touches(&env->global_geometry, &neighbor->global_geometry, BORDER_TYPE_LEFT | BORDER_TYPE_CEILING | BORDER_TYPE_FLOOR)) {
            int32_t right_offset = neighbor->global_geometry.x + neighbor->global_geometry.width - env->global_geometry.x;
            if (right_offset > max_right_offset) {
                max_right_offset = right_offset;
                max_width = neighbor->global_geometry.width + env->global_geometry.width;
            }
            new_border_mask |= BORDER_TYPE_RIGHT;
            if (neighbor->global_geometry.y == env->global_geometry.y) {
                ceiling_aligned = true;
            }
            if (neighbor->global_geometry.y + neighbor->global_geometry.height == env->global_geometry.y + env->global_geometry.height) {
                floor_aligned = true;
            }
            continue;
        }

        // Check if the neighbor touches the top border
        if (bounding_box_touches(&env->global_geometry, &neighbor->global_geometry, BORDER_TYPE_RIGHT | BORDER_TYPE_LEFT | BORDER_TYPE_FLOOR)) {
            int32_t top_offset = env->global_geometry.y - neighbor->global_geometry.y;
            if (top_offset > max_top_offset) {
                max_top_offset = top_offset;
                max_height = neighbor->global_geometry.height + env->global_geometry.height;
            }
            new_border_mask |= BORDER_TYPE_CEILING;
            if (neighbor->global_geometry.x == env->global_geometry.x) {
                left_aligned = true;
            }
            if (neighbor->global_geometry.x + neighbor->global_geometry.width == env->global_geometry.x + env->global_geometry.width) {
                right_aligned = true;
            }
            continue;
        }

        // Check if the neighbor touches the bottom border
        if (bounding_box_touches(&env->global_geometry, &neighbor->global_geometry, BORDER_TYPE_RIGHT | BORDER_TYPE_CEILING | BORDER_TYPE_LEFT)) {
            int32_t bottom_offset = neighbor->global_geometry.y + neighbor->global_geometry.height - env->global_geometry.y;
            if (bottom_offset > max_bottom_offset) {
                max_bottom_offset = bottom_offset;
                max_height = neighbor->global_geometry.height + env->global_geometry.height;
            }
            new_border_mask |= BORDER_TYPE_FLOOR;
            if (neighbor->global_geometry.x == env->global_geometry.x) {
                left_aligned = true;
            }
            if (neighbor->global_geometry.x + neighbor->global_geometry.width == env->global_geometry.x + env->global_geometry.width) {
                right_aligned = true;
            }
            continue;
        }
    }

    // Update the advertised geometry based on the maximum dimensions and offsets
    env->advertised_geometry.x = env->workarea_geometry.x - max_left_offset;
    env->advertised_geometry.y = env->workarea_geometry.y - max_top_offset;
    env->advertised_geometry.width = max_width;
    env->advertised_geometry.height = max_height;
    env->ceiling_aligned = ceiling_aligned;
    env->floor_aligned = floor_aligned;
    env->left_aligned = left_aligned;
    env->right_aligned = right_aligned;

    // Mask the borders that are touched by neighbors
    env->border_mask = new_border_mask;
    INFO("------------[Environment %d recalculate advertised geometry]---------------", env->id);
    INFO("New border mask is %d", env->border_mask);
    INFO("Advertised geometry is %d, %d, %d, %d", env->advertised_geometry.x, env->advertised_geometry.y, env->advertised_geometry.width, env->advertised_geometry.height);
    INFO("Geometry alignment: %s%s%s%s", ceiling_aligned ? "ceiling " : "", floor_aligned ? "floor " : "", left_aligned ? "left " : "", right_aligned ? "right" : "");
    INFO("--------------------------------------------------------------------------");
}

void environment_announce_neighbor(environment_t* environment, environment_t* neighbor)
{
    if (!environment || !neighbor) return;
    if (!config_get_unified_outputs()) return;
    if (environment == neighbor) return;

    INFO("Announcing neighbor %d to %d", neighbor->id, environment->id);

    if (environment->neighbors) {
        if (list_find(environment->neighbors, neighbor) != UINT32_MAX) {
            WARN("waht?");
            return;
        }
    }
    list_add(environment->neighbors, neighbor);
    environment_recalculate_advertised_geometry(environment);
}

void environment_widthdraw_neighbor(environment_t* environment, environment_t* neighbor)
{
    if (!environment || !neighbor) return;
    if (!config_get_unified_outputs()) return;
    if (environment == neighbor) return;

    INFO("Widthdrawing neighbor %d from %d", neighbor->id, environment->id);

    uint32_t index = list_find(environment->neighbors, neighbor);
    if (index != UINT32_MAX) {
        list_remove(environment->neighbors, index);
        environment_recalculate_advertised_geometry(environment);
    }
}

bool environment_neighbor_border(environment_t* environment, int32_t x, int32_t y)
{
    if (!environment) return false;
    if (!environment->is_ready) return false;

    int32_t global_x = x, global_y = y;
    environment_to_global_coordinates(environment, &global_x, &global_y);

    for (size_t i = 0; i < list_size(environment->neighbors); i++) {
        environment_t* neighbor = list_get(environment->neighbors, i);
        if (!neighbor) continue;
        int32_t collision_at = BORDER_TYPE(check_collision_at(&neighbor->global_geometry, global_x, global_y, 0));
        if (collision_at) return true;
    }
    return false;
}

void environment_set_affordance_manager(environment_t* environment, struct mascot_affordance_manager* manager)
{
    if (!environment) return;
    environment->mascot_manager.affordances = manager;
    for (size_t i = 0; i < list_size(environment->mascot_manager.referenced_mascots); i++) {
        struct mascot* mascot = list_get(environment->mascot_manager.referenced_mascots, i);
        if (!mascot) continue;
        mascot_attach_affordance_manager(mascot, manager);
    }
}

int32_t environment_workarea_coordinate_aligned(environment_t* env, int32_t border_type, int32_t alignment_type)
{
    if (!env) return 0;
    if (!env->is_ready) return 0;

    int32_t retval = 0;
    if (border_type & BORDER_TYPE_LEFT) {
        retval = env->workarea_geometry.x;
        if (alignment_type & BORDER_TYPE_FLOOR && env->ceiling_aligned) {
            retval = env->advertised_geometry.x;
        } else if (alignment_type & BORDER_TYPE_CEILING && env->floor_aligned) {
            retval = env->advertised_geometry.x;
        }
    } else if (border_type & BORDER_TYPE_RIGHT) {
        retval = env->workarea_geometry.x + env->workarea_geometry.width;
        if (alignment_type & BORDER_TYPE_FLOOR && env->ceiling_aligned) {
            retval = env->advertised_geometry.x + env->advertised_geometry.width;
        } else if (alignment_type & BORDER_TYPE_CEILING && env->floor_aligned) {
            retval = env->advertised_geometry.x + env->advertised_geometry.width;
        }
    } else if (border_type & BORDER_TYPE_CEILING) {
        retval = env->advertised_geometry.y;
        if (alignment_type & BORDER_TYPE_LEFT && env->left_aligned) {
            retval = env->advertised_geometry.y;
        } else if (alignment_type & BORDER_TYPE_RIGHT && env->right_aligned) {
            retval = env->advertised_geometry.y;
        }
    } else if (border_type & BORDER_TYPE_FLOOR) {
        retval = env->advertised_geometry.y + env->advertised_geometry.height;
        if (alignment_type & BORDER_TYPE_LEFT && env->left_aligned) {
            retval = env->advertised_geometry.y + env->advertised_geometry.height;
        } else if (alignment_type & BORDER_TYPE_RIGHT && env->right_aligned) {
            retval = env->advertised_geometry.y + env->advertised_geometry.height;
        }
    }

    return retval;
}

int32_t environment_workarea_width_aligned(environment_t* env, int32_t alignment_type)
{
    if (!env) return 0;
    if (!env->is_ready) return 0;

    int32_t retval = env->workarea_geometry.width;
    if (alignment_type & BORDER_TYPE_CEILING && env->ceiling_aligned) {
        retval = env->advertised_geometry.width;
    } else if (alignment_type & BORDER_TYPE_FLOOR && env->floor_aligned) {
        retval = env->advertised_geometry.width;
    }
    return retval;
}

int32_t environment_workarea_height_aligned(environment_t* env, int32_t alignment_type)
{
    if (!env) return 0;
    if (!env->is_ready) return 0;

    int32_t retval = env->workarea_geometry.height;
    if (alignment_type & BORDER_TYPE_LEFT && env->left_aligned) {
        retval = env->advertised_geometry.height;
    } else if (alignment_type & BORDER_TYPE_RIGHT && env->right_aligned) {
        retval = env->advertised_geometry.height;
    }
    return retval;
}

void environment_recalculate_ie_attachement(environment_t* env, bool is_active, struct bounding_box new_geometry) {
    if (!env || !env->is_ready) return;
    pthread_scoped_lock(mascot_lock, &env->mascot_manager.mutex);

    for (uint32_t i = 0; i < env->mascot_manager.referenced_mascots->entry_count; i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (!mascot) continue;

        int32_t x = mascot->X->value.i;
        int32_t y = yconv(env, mascot->Y->value.i);
        int32_t new_x = x;
        int32_t new_y = y;

        if (!mascot_is_on_ie(mascot)) continue;
        if (check_collision_at(&env->workarea_geometry, x, y, 0)) {
            continue;
        }
        if (!is_active) {
            mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
            continue;
        }

        if (mascot_is_on_ie_left(mascot) && (active_ie.geometry.x != environment_workarea_left(env))) {
            if (x != new_geometry.x) {
                new_x = new_geometry.x;
            }
            if (active_ie.geometry.y != new_geometry.y) {
                new_y += (new_geometry.y - active_ie.geometry.y);
            }
        } else if (mascot_is_on_ie_right(mascot) && (active_ie.geometry.x + active_ie.geometry.width != environment_workarea_right(env))) {
            if (x != new_geometry.x + new_geometry.width) {
                new_x = new_geometry.x + new_geometry.width;
            }
            if (active_ie.geometry.y != new_geometry.y) {
                new_y += (new_geometry.y - active_ie.geometry.y);
            }
        } else if (mascot_is_on_ie_top(mascot) && (active_ie.geometry.y != environment_workarea_top(env))) {
            if (y != new_geometry.y) {
                new_y = new_geometry.y;
            }
            if (active_ie.geometry.x != new_geometry.x) {
                new_x += (new_geometry.x - active_ie.geometry.x);
            }
        } else if (mascot_is_on_ie_bottom(mascot) && (active_ie.geometry.y+active_ie.geometry.height != environment_workarea_bottom(env))) {
            if (y != new_geometry.y + new_geometry.height) {
                new_y = new_geometry.y + new_geometry.height;
            }
            if (active_ie.geometry.x != new_geometry.x) {
                new_x += (new_geometry.x - active_ie.geometry.x);
            }
        }

        if (abs(new_x - x) > 25 || abs(new_y - y) > 25) {
            mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
            continue;
        }

        mascot_moved(mascot, new_x, yconv(env, new_y));
        mascot->TargetX->value.i += new_x - x;
        mascot->TargetY->value.i += yconv(env, new_y) - yconv(env, y);

        if (!config_get_interpolation_framerate()) {
            environment_subsurface_reset_interpolation(mascot->subsurface);
            environment_subsurface_set_position(mascot->subsurface, new_x, new_y);
        } else {
            mascot->subsurface->interpolation_data.new_x = new_x;
            mascot->subsurface->interpolation_data.new_y = new_y;
            mascot->subsurface->interpolation_data.prev_x = x;
            mascot->subsurface->interpolation_data.prev_y = y;
            mascot->subsurface->interpolation_data.x = x;
            mascot->subsurface->interpolation_data.y = y;
        }
    }
}

void environment_set_active_ie(bool is_active, struct bounding_box geometry)
{
    active_ie.is_active = is_active;
    active_ie.geometry = geometry;
    active_ie.geometry.type = OUTER_COLLISION;
}

struct bounding_box environment_get_active_ie(environment_t *environment)
{
    if (!environment) return (struct bounding_box){0};
    if (!environment->is_ready) return (struct bounding_box){0};
    if (!active_ie.is_active) return (struct bounding_box){0};

    if (!config_get_unified_outputs()) {
        if (!bounding_boxes_intersect(&environment->global_geometry, &active_ie.geometry, NULL)) return (struct bounding_box){0};
    }
    return (struct bounding_box){
        .type = OUTER_COLLISION,
        .x = active_ie.geometry.x - environment->global_geometry.x,
        .y = active_ie.geometry.y - environment->global_geometry.y,
        .width = active_ie.geometry.width,
        .height = active_ie.geometry.height
    };
}

bool environment_ie_is_active()
{
    return active_ie.is_active;
}

void environment_mascot_detach_ie_movers(environment_t* env)
{
    if (!env) return;
    pthread_scoped_lock(mascot_lock, &env->mascot_manager.mutex);
    for (uint32_t i = 0; i < env->mascot_manager.referenced_mascots->entry_count; i++) {
        struct mascot* mascot = list_get(env->mascot_manager.referenced_mascots, i);
        if (!mascot) continue;
        pthread_scoped_lock(tick_lock, &mascot->tick_lock);

        if (mascot->state == mascot_state_ie_fall || mascot->state == mascot_state_ie_walk || mascot->state == mascot_state_ie_throw) mascot_set_behavior(mascot, mascot->prototype->fall_behavior);
    }
}
