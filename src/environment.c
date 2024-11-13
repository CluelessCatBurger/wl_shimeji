/*
    environment.c - wl_shimeji's environment handling

    Copyright (C) 2024  CluelessCatBurger <github.com/CluelessCatBurger>

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
#include <sys/mman.h>
#include "mascot_atlas.h"
#include "mascot.h"
#include "wayland_includes.h"
#include "layer_surface.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include "environment.h"
#include <linux/input-event-codes.h>
#include <wayland-util.h>
#include <errno.h>

static void block_until_synced();

static char error_m[1024] = {0};
static uint16_t error_offt = 0;
static enum environment_init_status init_status = ENV_NOT_INITIALIZED;

struct wl_display* display = NULL;
struct wl_registry* registry = NULL;
struct wl_compositor* compositor = NULL;
struct wl_shm* shm_manager = NULL;
struct wl_subcompositor* subcompositor = NULL;
struct wl_seat* seat = NULL;

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

static uint32_t wl_refcounter = 0;

void (*new_environment)(environment_t*) = NULL;
void (*rem_environment)(environment_t*) = NULL;

struct wl_buffer* anchor_buffer = NULL;

static uint32_t display_id = 0;

struct wl_region* empty_region = NULL;

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
    environment_subsurface_t* grabbed_surface;
    environment_subsurface_t* root_environment_subsurface;
    bool pending_commit;
    bool has_active_window;
    int32_t interactive_window_count;
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

struct environment_pointer {
    wl_fixed_t x, y;
    wl_fixed_t temp_x, temp_y;
    wl_fixed_t mascot_x, mascot_y;
    wl_fixed_t temp_dx, temp_dy;
    wl_fixed_t dx, dy;
    uint32_t last_tick;
    environment_subsurface_t* grabbed_surface;
    uint8_t device_type;

    // Devices
    struct wl_pointer* pointer;
    struct wl_touch* touch;
    struct wl_tablet_tool* tablet_tool;

    uint32_t enter_serial;

    uint8_t button_state;

    struct wl_surface* above_surface;

    bool select_active;
    void (*select_callback)(environment_t*, int32_t, int32_t, environment_subsurface_t*, void*);
    void* select_data;

    // Cursor shapes
    struct wp_cursor_shape_device_v1* cursor_shape_device;
    struct wp_cursor_shape_device_v1* cursor_shape_device_tablet;
    enum wp_cursor_shape_device_v1_shape cursor_shape;

};

environment_pointer_t active_pointer = {0};
static uint32_t yconv(environment_t* env, uint32_t y);

// Helper structs -----------------------------------------------------------

struct envs_queue {
    uint8_t envs_count;
    environment_t* envs[16];
};

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
    env->output.name = name;
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
    UNUSED(serial);
    UNUSED(data);

    active_pointer.enter_serial = serial;
    active_pointer.pointer = pointer;
    if (active_pointer.cursor_shape_device && active_pointer.pointer) {
        wp_cursor_shape_device_v1_set_shape(active_pointer.cursor_shape_device, active_pointer.enter_serial, active_pointer.cursor_shape);
    }

    struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(surface);
    if (!callbacks) {
        return;
    }

    if (!callbacks->pointer_listener) {
        return;
    }

    if (!callbacks->pointer_listener->enter) {
        return;
    }

    callbacks->pointer_listener->enter(callbacks->data, pointer, serial, surface, x, y);
}

static void mascot_on_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(serial);
    environment_subsurface_t* env_surface = (environment_subsurface_t*)data;

    if (!env_surface) {
        active_pointer.x = wl_fixed_to_int(x);
        active_pointer.y = wl_fixed_to_int(y);
    } else {
        active_pointer.mascot_x = wl_fixed_to_int(x);
        active_pointer.mascot_y = wl_fixed_to_int(y);
    }
    active_pointer.temp_x = wl_fixed_to_int(x);
    active_pointer.temp_y = wl_fixed_to_int(y);
    active_pointer.pointer = pointer;
    active_pointer.device_type = CURRENT_DEVICE_TYPE_MOUSE;
    active_pointer.above_surface = surface;
}

static void on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
    UNUSED(data);

    active_pointer.pointer = NULL;
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->leave) {
            return;
        }
        callbacks->pointer_listener->leave(callbacks->data, pointer, serial, surface);
    }

    active_pointer.above_surface = NULL;
}

static void on_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(data);

    active_pointer.temp_x = wl_fixed_to_int(x);
    active_pointer.temp_y = wl_fixed_to_int(y);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->motion) {
            return;
        }
        callbacks->pointer_listener->motion(callbacks->data, pointer, time, x, y);
    }
}

static void on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    UNUSED(data);

    if (active_pointer.grabbed_surface && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }
    if (!active_pointer.grabbed_surface && state == WL_POINTER_BUTTON_STATE_RELEASED && button == BTN_LEFT) {
        return;
    }

    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->button) {
            return;
        }
        callbacks->pointer_listener->button(callbacks->data, pointer, serial, time, button, state);
    } else {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        {} else {
            if (button == BTN_LEFT) {
                if (active_pointer.grabbed_surface) {
                    mascot_drag_ended(active_pointer.grabbed_surface->mascot);
                }
                active_pointer.button_state &= ~1;
            } else if (button == BTN_MIDDLE) {
                active_pointer.button_state &= ~2;
            }
        }
    }
}

static void environment_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    UNUSED(serial);
    UNUSED(time);
    UNUSED(pointer);
    environment_subsurface_t* envs = (environment_subsurface_t*)data;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == BTN_LEFT) {
            if (active_pointer.select_active) {
                active_pointer.select_active = false;
                active_pointer.select_callback(envs->env, active_pointer.x, yconv(envs->env, active_pointer.y), envs, active_pointer.select_data);
                active_pointer.cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
                if (active_pointer.cursor_shape_device && active_pointer.pointer) {
                    wp_cursor_shape_device_v1_set_shape(active_pointer.cursor_shape_device, active_pointer.enter_serial, active_pointer.cursor_shape);
                }
                INFO("SELECTED surface %p, at %d,%d", envs, active_pointer.x, yconv(envs->env, active_pointer.y));
            }
        }
    }
}

static void mascot_on_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
    UNUSED(pointer);
    UNUSED(serial);
    UNUSED(surface);

    environment_subsurface_t* env_surface = (environment_subsurface_t*)data;

    if (active_pointer.button_state & 2) {
        active_pointer.button_state &= ~2;
        mascot_hotspot_hold(env_surface->mascot, 0, 0, true);
    }
}

static void mascot_on_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{

    environment_subsurface_t* env_surface = (environment_subsurface_t*)data;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == BTN_LEFT) {
            if (env_surface) {
                if (env_surface->env->root_environment_subsurface != env_surface && !active_pointer.select_active) {
                    mascot_drag_started(env_surface->mascot, &active_pointer);
                    active_pointer.button_state |= 1;
                    active_pointer.x = env_surface->mascot->X->value.i + active_pointer.mascot_x + env_surface->pose->anchor_x;
                    active_pointer.y = env_surface->mascot->Y->value.i + active_pointer.mascot_y + env_surface->pose->anchor_y;
                } else {
                    environment_on_pointer_button(data, pointer, serial, time, button, state);
                }
            }
        } else if (button == BTN_MIDDLE) {
            if (env_surface) {
                if (env_surface->env->root_environment_subsurface != env_surface) {
                    mascot_hotspot_hold(env_surface->mascot, active_pointer.mascot_x, active_pointer.mascot_y, false);
                    active_pointer.button_state |= 2;
                    active_pointer.x = env_surface->mascot->X->value.i + active_pointer.mascot_x + env_surface->pose->anchor_x;
                    active_pointer.y = env_surface->mascot->Y->value.i + active_pointer.mascot_y + env_surface->pose->anchor_y;
                }
            }
        }
    } else {
        if (button == BTN_LEFT) {
            active_pointer.button_state &= ~1;
            if (active_pointer.grabbed_surface) {
                active_pointer.dx = (active_pointer.x - active_pointer.temp_dx);
                active_pointer.dy = (active_pointer.y - active_pointer.temp_dy);
                mascot_drag_ended(active_pointer.grabbed_surface->mascot);
            }
        } else if (button == BTN_MIDDLE) {
            active_pointer.button_state &= ~2;
            mascot_hotspot_hold(env_surface->mascot, active_pointer.mascot_x, active_pointer.mascot_y, true);
        }
    }


}

static void on_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis) {
            return;
        }
        callbacks->pointer_listener->axis(callbacks->data, pointer, time, axis, value);
    }
}

static void on_pointer_frame(void* data, struct wl_pointer* pointer)
{
    UNUSED(data);

    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->frame) {
            return;
        }
        callbacks->pointer_listener->frame(callbacks->data, pointer);
    }
}

static void mascot_on_pointer_frame(void* data, struct wl_pointer* pointer)
{
    UNUSED(pointer);
    environment_subsurface_t* env_surface = (environment_subsurface_t*)data;
    if (env_surface) {
        if (env_surface->env->root_environment_subsurface != env_surface) {
            active_pointer.mascot_x = active_pointer.temp_x;
            active_pointer.mascot_y = active_pointer.temp_y;
            // Set also global position
            // mascot_x/y is surface-local position, if we above a surface, temo_x/y is local position too
            // Position of mascot surface is sum of mascot->X/Y->value.i and surface->pose->anchor_x/y
            // To get global position, we need to add mascot_x/y to that sum
            if (env_surface->mascot) {
                active_pointer.x = env_surface->mascot->X->value.i + active_pointer.mascot_x + env_surface->pose->anchor_x / env_surface->env->scale;
                active_pointer.y = yconv(env_surface->env, env_surface->mascot->Y->value.i) + active_pointer.mascot_y + env_surface->pose->anchor_y / env_surface->env->scale;
            }
        } else {
            active_pointer.x = active_pointer.temp_x;
            active_pointer.y = active_pointer.temp_y;
        }
    }

}

static void on_pointer_axis_source(void* data, struct wl_pointer* pointer, uint32_t axis_source)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis_source) {
            return;
        }
        callbacks->pointer_listener->axis_source(callbacks->data, pointer, axis_source);
    }
}

static void on_pointer_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis_stop) {
            return;
        }
        callbacks->pointer_listener->axis_stop(callbacks->data, pointer, time, axis);
    }
}

static void on_pointer_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t discrete)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis_discrete) {
            return;
        }
        callbacks->pointer_listener->axis_discrete(callbacks->data, pointer, axis, discrete);
    }
}

static void mascot_on_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(time);
    UNUSED(value);
    environment_subsurface_t* env_surface = (environment_subsurface_t*)data;
    if (env_surface) {
        mascot_hotspot_click(env_surface->mascot, active_pointer.mascot_x, active_pointer.mascot_y);
    }
}

static void on_pointer_axis_value120(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t value)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis_value120) {
            return;
        }
        callbacks->pointer_listener->axis_value120(callbacks->data, pointer, axis, value);
    }
}

static void on_pointer_axis_relative_direction(void* data, struct wl_pointer* pointer, uint32_t axis, uint32_t direction)
{
    UNUSED(data);
    if (active_pointer.above_surface) {
        struct environment_callbacks* callbacks = (struct environment_callbacks*)wl_surface_get_user_data(active_pointer.above_surface);
        if (!callbacks) {
            return;
        }
        if (!callbacks->pointer_listener) {
            return;
        }
        if (!callbacks->pointer_listener->axis_relative_direction) {
            return;
        }
        callbacks->pointer_listener->axis_relative_direction(callbacks->data, pointer, axis, direction);
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

static const struct wl_pointer_listener mascot_pointer_listener = {
    .enter = mascot_on_pointer_enter,
    .button = mascot_on_pointer_button,
    .frame = mascot_on_pointer_frame,
    .axis = mascot_on_pointer_axis,
    .leave = mascot_on_pointer_leave
};

static void on_seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
    UNUSED(seat);
    UNUSED(data);
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        struct wl_pointer* pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &wl_pointer_listener, NULL);
        if (cursor_shape_manager) {
            active_pointer.cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(cursor_shape_manager, pointer);
            active_pointer.cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        }
    }
    // if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
    //     struct wl_touch* touch = wl_seat_get_touch(seat);
    //     wl_touch_add_listener(touch, &wl_touch_listener, NULL);
    // }
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
    UNUSED(data);
    UNUSED(tool);
    UNUSED(type);
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
    UNUSED(data);
    zwp_tablet_tool_v2_destroy(tool);
    if (active_pointer.device_type == CURRENT_DEVICE_TYPE_PEN && active_pointer.button_state & 1 << 4 && active_pointer.grabbed_surface) {
        mascot_drag_ended(active_pointer.grabbed_surface->mascot);
        active_pointer.button_state &= ~(1 << 4);
        active_pointer.grabbed_surface = NULL;
    }
}

static void on_tool_proximity_in(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, struct zwp_tablet_v2* tablet, struct wl_surface* surface)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(surface);
    UNUSED(tablet);
    UNUSED(serial);

    if (active_pointer.grabbed_surface) return;

    active_pointer.device_type = CURRENT_DEVICE_TYPE_PEN;
    active_pointer.above_surface = surface;
}

static void on_tool_proximity_out(void* data, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(data);
    UNUSED(tool);

    if (active_pointer.grabbed_surface) {
        mascot_drag_ended(active_pointer.grabbed_surface->mascot);
        active_pointer.grabbed_surface = NULL;
    }
    active_pointer.above_surface = NULL;
}

static void on_tool_down(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(serial);

    if (active_pointer.grabbed_surface) return;

    environment_subsurface_t* env_surface = NULL;
    if (active_pointer.above_surface) env_surface = wl_surface_get_user_data(active_pointer.above_surface);
    if (env_surface) {
        active_pointer.device_type = CURRENT_DEVICE_TYPE_PEN;
        active_pointer.button_state |= 1 << 4;
        active_pointer.x = env_surface->mascot->X->value.i + active_pointer.mascot_x + env_surface->pose->anchor_x;
        active_pointer.y = env_surface->mascot->Y->value.i + active_pointer.mascot_y + env_surface->pose->anchor_y;
        active_pointer.temp_x = env_surface->mascot->X->value.i + active_pointer.mascot_x + env_surface->pose->anchor_x;
        active_pointer.temp_y = env_surface->mascot->Y->value.i + active_pointer.mascot_y + env_surface->pose->anchor_y;
        mascot_drag_started(env_surface->mascot, &active_pointer);
    }
}

static void on_tool_up(void* data, struct zwp_tablet_tool_v2* tool)
{
    UNUSED(data);
    UNUSED(tool);

    if (active_pointer.grabbed_surface) {
        mascot_drag_ended(active_pointer.grabbed_surface->mascot);
        active_pointer.grabbed_surface = NULL;
        active_pointer.above_surface = NULL;
    }
    active_pointer.button_state &= ~(1 << 4);
}

static void on_tool_button(void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, uint32_t button, uint32_t state)
{
    UNUSED(data);
    UNUSED(tool);
    UNUSED(serial);
    UNUSED(button);
    UNUSED(state);
}

static void on_tool_motion(void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y)
{
    UNUSED(data);
    UNUSED(tool);

    if (active_pointer.grabbed_surface) {
        active_pointer.temp_x = wl_fixed_to_int(x);
        active_pointer.temp_y = wl_fixed_to_int(y);
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

    environment_subsurface_t* env_surface = NULL;
    if (active_pointer.above_surface) env_surface = wl_surface_get_user_data(active_pointer.above_surface);

    if (active_pointer.grabbed_surface) {
        active_pointer.x = active_pointer.temp_x;
        active_pointer.y = active_pointer.temp_y;
        return;
    }

    if (env_surface) {
        if (env_surface->env->root_environment_subsurface == env_surface) {
            active_pointer.x = active_pointer.temp_x;
            active_pointer.y = active_pointer.temp_y;
        } else {
            active_pointer.mascot_x = active_pointer.temp_x - env_surface->mascot->X->value.i - env_surface->pose->anchor_x;
            active_pointer.mascot_y = active_pointer.temp_y - env_surface->mascot->Y->value.i - env_surface->pose->anchor_y;
        }
    }
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
    zwp_tablet_tool_v2_add_listener(tool, &zwp_tablet_tool_v2_listener, NULL);
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


static const struct wp_fractional_scale_v1_listener fractional_scale_manager_v1_listener = {
    .preferred_scale = on_preffered_scale,
};

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
        wl_output_add_listener(output, &wl_output_listener, (void*)env);
        new_environment(env);
        envs->envs[envs->envs_count++] = env;
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

static void on_global (void* data, struct wl_registry* registry, uint32_t id, const char* iface_name, uint32_t version)
{
    UNUSED(registry);
    if (!strcmp(iface_name, wl_compositor_interface.name)) {
        handle_compositor(data, id, version);
    } else if (!strcmp(iface_name, wl_shm_interface.name)) {
        handle_shm_manager(data, id, version);
    } else if (!strcmp(iface_name, wl_subcompositor_interface.name)) {
        handle_subcompositor(data, id, version);
    } else if (!strcmp(iface_name, zwlr_layer_shell_v1_interface.name)) {
        handle_wlr_layer_shell(data, id, version);
    } else if (!strcmp(iface_name, zwp_tablet_manager_v2_interface.name)) {
        handle_tablet_manager(data, id, version);
    } else if (!strcmp(iface_name, wl_output_interface.name)) {
        handle_output(data, id, version);
    } else if (!strcmp(iface_name, wl_seat_interface.name)) {
        handle_seat(data, id, version);
    } else if (!strcmp(iface_name, wp_viewporter_interface.name)) {
        handle_viewporter(data, id, version);
    } else if (!strcmp(iface_name, wp_fractional_scale_manager_v1_interface.name)) {
        handle_fractional_scale_manager(data, id, version);
    } else if (!strcmp(iface_name, wp_cursor_shape_manager_v1_interface.name)) {
        handle_cursor_shape(data, id, version);
    }
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
	UNUSED(data);
    UNUSED(registry);
    UNUSED(id);
    WARN("Unexpected removal of global! id:%u", id);
}

static const struct wl_registry_listener registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove
};

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

// Helper function --------------------------------------------------------------

static void block_until_synced()
{
    bool lock = false;
    struct wl_callback* callback = wl_display_sync(display);
    wl_callback_add_listener(callback, &sync_listener, &lock);
    wl_display_roundtrip(display);
    while(wl_display_dispatch(display) != -1 && lock);
}

uint32_t yconv(environment_t* env, uint32_t y)
{
    return env->height - y;
}

static void environment_dimensions_changed_callback(uint32_t width, uint32_t height, void* data)
{
    environment_t* env = (environment_t*)data;
    env->width = width;
    env->height = height;
}

// Public functions ------------------------------------------------------------

enum environment_init_status environment_init(void(*new_listener)(environment_t*), void(*rem_listener)(environment_t*))
{
    // Wayland display connection and etc
    display = wl_display_connect(NULL);
    if (!display) {
        snprintf(error_m, 1024, "Environment initialization failed: Failed to connect to Wayland display");
        init_status = ENV_INIT_ERROR_DISPLAY;
        return ENV_INIT_ERROR_DISPLAY;
    }

    new_environment = new_listener;
    rem_environment = rem_listener;

    struct envs_queue envs = {0};

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, (void*)&envs);
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

    for (size_t i = 0; i < envs.envs_count; i++) {
        environment_t* env = envs.envs[i];
        env->root_surface = layer_surface_create(env->output.output, LAYER_TYPE_OVERLAY);
        if (!env->root_surface) {
            WARN("FAILED TO CREATE LAYER SURFACE ON OUTPUT %u aka %s", env->id, env->output.name);
            if (rem_environment) {
                rem_environment(env);
            }
            continue;
        }
        layer_surface_set_dimensions_callback(env->root_surface, environment_dimensions_changed_callback, env);
        layer_surface_map(env->root_surface);
        block_until_synced();
        if (env->root_surface->configure_serial) {
            env->is_ready = true;
            env->root_environment_subsurface = environment_create_subsurface(env);

            env->scale = env->output.scale;

            if (fractional_manager) {
                env->root_environment_subsurface->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(fractional_manager, env->root_environment_subsurface->surface);
                if (!env->root_environment_subsurface->fractional_scale) {
                    wl_surface_destroy(env->root_environment_subsurface->surface);
                    free(env->root_environment_subsurface);
                    error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Failed to get fractional scale for root environment subsurface\n");
                    init_status = ENV_INIT_ERROR_GENERIC;
                    return ENV_INIT_ERROR_GENERIC;
                }
                wp_fractional_scale_v1_add_listener(env->root_environment_subsurface->fractional_scale, &fractional_scale_manager_v1_listener, env);
            }

            struct environment_callbacks* callbacks = (struct environment_callbacks*)calloc(1, sizeof(struct environment_callbacks));
            if (!callbacks) {
                error_offt += snprintf(error_m + error_offt, 1024 - error_offt, "Failed to allocate memory for environment callbacks\n");
                init_status = ENV_INIT_ERROR_OOM;
                return init_status;
            }
            callbacks->data = (void*)env->root_environment_subsurface;
            callbacks->pointer_listener = &mascot_pointer_listener;

            wl_surface_set_user_data(env->root_surface->surface, (void*)callbacks);
            wl_surface_attach(env->root_environment_subsurface->surface, anchor_buffer, 0, 0);
        }
        environment_commit(env);
    }

    if (tablet_manager) {
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

void environment_dispatch()
{
    wl_display_dispatch(display);
}

void environment_unlink(environment_t *env)
{
    if (env->output.output) {
        wl_output_destroy(env->output.output);
    }

    if (env->root_environment_subsurface->surface) {
        wl_subsurface_destroy(env->root_environment_subsurface->subsurface);
    }

    if (env->root_environment_subsurface->surface) {
        wl_surface_destroy(env->root_environment_subsurface->surface);
    }

    if (env->root_environment_subsurface) {
        environment_destroy_subsurface(env->root_environment_subsurface);
    }
    if (env->root_surface) {
        layer_surface_destroy(env->root_surface);
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

// ENV API ---------------------------------------------------------------------

enum environment_border_type environment_get_border_type(environment_t *env, int32_t x, int32_t y)
{
    if (!env->is_ready) return environment_border_type_invalid;

    y = yconv(env, y);

    // Detect type of border by coordinates using output size (width, height)
    if (y <= 0) return environment_border_type_ceiling;
    else if (x >= (int32_t)env->width || x <= 0) return environment_border_type_wall;
    else if (y >= (int32_t)env->height) return environment_border_type_floor;
    else return environment_border_type_none;
}

// Surface API -----------------------------------------------------------------

environment_subsurface_t* environment_create_subsurface(environment_t* env)
{
    if (!env->is_ready) return NULL;

    environment_subsurface_t* surface = (environment_subsurface_t*)calloc(1, sizeof(environment_subsurface_t));
    if (!surface) return NULL;

    surface->surface = wl_compositor_create_surface(compositor);
    if (!surface->surface) {
        free(surface);
        return NULL;
    }

    surface->subsurface = wl_subcompositor_get_subsurface(subcompositor, surface->surface, env->root_surface->surface);
    if (!surface->subsurface) {
        wl_surface_destroy(surface->surface);
        free(surface);
        return NULL;
    }

    struct environment_callbacks* callbacks = (struct environment_callbacks*)calloc(1, sizeof(struct environment_callbacks));
    if (!callbacks) {
        wl_surface_destroy(surface->surface);
        free(surface);
        return NULL;
    }
    callbacks->data = (void*)surface;
    callbacks->pointer_listener = &mascot_pointer_listener;

    if (viewporter) {
        surface->viewport = wp_viewporter_get_viewport(viewporter, surface->surface);
        if (!surface->viewport) {
            wl_surface_destroy(surface->surface);
            free(surface);
            return NULL;
        }
    }

    if (env->output.scale && !fractional_manager && !viewporter) {
        wl_surface_set_buffer_scale(surface->surface, env->output.scale);
    }

    wl_surface_set_user_data(surface->surface, (void*)callbacks);

    env->pending_commit = true;

    surface->env = env;
    return surface;
}

void environment_destroy_subsurface(environment_subsurface_t* surface)
{
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



    wl_surface_attach(surface->surface, sprite->buffer, 0, 0);
    wl_surface_damage(surface->surface, 0, 0 , INT32_MAX, INT32_MAX);
    if (active_pointer.grabbed_surface != surface) {
        wl_surface_set_input_region(surface->surface, sprite->input_region);
    }

    if (!surface->pose) {
        wl_subsurface_place_below(surface->subsurface, surface->env->root_environment_subsurface->surface);
        if (surface->mascot) {
            environment_subsurface_move(surface, surface->mascot->X->value.i, surface->mascot->Y->value.i, false);
        }
        wl_surface_commit(surface->surface);
        wl_display_flush(display);
        wl_display_dispatch_pending(display);
    }
    surface->pose = pose;

    surface->width = sprite->width;
    surface->height = sprite->height;

    wl_surface_commit(surface->surface);

    if (viewporter) {
        wp_viewport_set_source(
            surface->viewport,
            0, 0,
            wl_fixed_from_int(sprite->width),
            wl_fixed_from_int(sprite->height)
        );
        wp_viewport_set_destination(
            surface->viewport,
            sprite->width / surface->env->scale,
            sprite->height / surface->env->scale
        );
        wl_surface_commit(surface->surface);
    }

    environment_subsurface_move(surface, surface->mascot->X->value.i, surface->mascot->Y->value.i, false);

    surface->env->pending_commit = true;
}


bool isCollinearAndMovingAlong(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                int32_t z1, int32_t w1, int32_t z2, int32_t w2) {
    // Check if points (x1, y1), (x2, y2) and (z1, w1), (z2, w2) are collinear
    int32_t dx1 = x2 - x1;
    int32_t dy1 = y2 - y1;
    int32_t dx2 = z2 - z1;
    int32_t dy2 = w2 - w1;

    // Collinearity check using cross product
    if (dx1 * dy2 != dy1 * dx2) {
        return false; // Not collinear
    }

    // Check if the second segment is aligned and moving along
    return (z1 >= x1 && z1 <= x2 && w1 == y1 && w2 == y2) ||
           (z2 >= x1 && z2 <= x2 && w1 == y1 && w2 == y2) ||
           (w1 >= y1 && w1 <= y2 && z1 == x1 && z2 == x2);
}

bool segmentsTouchOrCross(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                          int32_t z1, int32_t w1, int32_t z2, int32_t w2) {
    // Check if the segments touch or cross
    // This checks if one endpoint of one segment lies on the other segment
    return ((z1 == x1 && w1 >= y1 && w1 <= y2) || (z1 == x2 && w1 >= y1 && w1 <= y2) ||
            (z2 == x1 && w2 >= y1 && w2 <= y2) || (z2 == x2 && w2 >= y1 && w2 <= y2));
}

bool segmentsIntersect(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       int32_t z1, int32_t w1, int32_t z2, int32_t w2,
                       int32_t* at_x, int32_t* at_y)
{
    // Check for collinearity and movement along the line
    if (isCollinearAndMovingAlong(x1, y1, x2, y2, z1, w1, z2, w2)) {
        return false; // Don't count overlaps while moving along
    }

    // Check for touching or crossing segments
    if (segmentsTouchOrCross(x1, y1, x2, y2, z1, w1, z2, w2)) {
        return false; // Avoid touching segments
    }

    // Calculate the direction vectors
    int32_t dx1 = x2 - x1;
    int32_t dy1 = y2 - y1;
    int32_t dx2 = z2 - z1;
    int32_t dy2 = w2 - w1;

    // Compute the determinant
    int32_t det = dx1 * dy2 - dy1 * dx2;
    if (det == 0) {
        // Lines are parallel
        return false;
    }

    // Calculate the parameters t and u using floating-point division
    float t = (float)((z1 - x1) * dy2 - (w1 - y1) * dx2) / det;
    float u = (float)((z1 - x1) * dy1 - (w1 - y1) * dx1) / det;

    // Check if the intersection point is within both segments
    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
        // Calculate the intersection point
        *at_x = x1 + (int32_t)(t * dx1);
        *at_y = y1 + (int32_t)(t * dy1);
        return true;
    }

    return false;
}

struct line {
    int32_t x, y, x1, x2;
    void* data;
};

void environment_fill_active_window_data(environment_t* env, struct line intersection_list[4]) {
    UNUSED(env);
    UNUSED(intersection_list);
}


enum environment_move_result environment_subsurface_move(environment_subsurface_t* surface, int32_t dx, int32_t dy, bool use_callback)
{
    if (!surface->surface) return environment_move_invalid;

    enum environment_move_result result = environment_move_ok;

    if (dy < 0) {
        dy = 0;
        result = environment_move_clamped;
    } else if (dy > (int32_t)surface->env->height) {
        dy = surface->env->height;
        result = environment_move_clamped;
    }

    if (dx < 0) {
        dx = 0;
        result = environment_move_clamped;
    } else if (dx > (int32_t)surface->env->width) {
        dx = surface->env->width;
        result = environment_move_clamped;
    }


    dy = yconv(surface->env, dy);

    bool has_active_window = surface->env->has_active_window;

    struct line intersection_list[surface->env->interactive_window_count*4];

    if (has_active_window) {
        environment_fill_active_window_data(surface->env, intersection_list);
    }

    if (surface->mascot && use_callback) mascot_moved(surface->mascot, dx, yconv(surface->env, dy));

    for (int i = 0; i < surface->env->interactive_window_count*4; i++) {
        if (segmentsIntersect(surface->x, surface->y, dx, dy, intersection_list[i].x, intersection_list[i].y, intersection_list[i].x1, intersection_list[i].x2, &dx, &dy)) {
            result = environment_move_clamped;
        }
    }


    if (environment_subsurface_set_position(surface, dx, dy) == environment_move_invalid) {
        return environment_move_invalid;
    }

    if (surface->mascot) {
        mascot_moved(surface->mascot, dx, yconv(surface->env, dy));
    }

    return result;

}

enum environment_move_result environment_subsurface_set_position(environment_subsurface_t* surface, int32_t dx, int32_t dy)
{
    if (!surface->surface) return environment_move_invalid;

    enum environment_move_result result = environment_move_ok;

    if (active_pointer.above_surface) {
        environment_subsurface_t* above_surface = wl_surface_get_user_data(active_pointer.above_surface);
        if (above_surface == surface) {
            active_pointer.mascot_x = active_pointer.x - (dx + surface->pose->anchor_x);
            active_pointer.mascot_y = active_pointer.y - (dy + surface->pose->anchor_y);
            if ((int)active_pointer.mascot_x < 0 || (int)active_pointer.mascot_x > (int)surface->pose->sprite[surface->mascot->LookingRight->value.i]->width / surface->env->scale
                || (int)active_pointer.mascot_y < 0 || (int)active_pointer.mascot_y > (int)surface->pose->sprite[surface->mascot->LookingRight->value.i]->height / surface->env->scale) {
                active_pointer.mascot_x = 0;
                active_pointer.mascot_y = 0;
                active_pointer.above_surface = NULL;
            }
        }
    }

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

uint32_t environment_screen_width(environment_t* env)
{
    return env->width;
}

uint32_t environment_screen_height(environment_t* env)
{
    return env->height;
}

uint32_t environment_workarea_width(environment_t* env)
{
    return env->width;
}

uint32_t environment_workarea_height(environment_t* env)
{
    return env->height;
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
    layer_surface_enable_input(surface->env->root_surface, true);
    wl_surface_set_input_region(surface->surface, empty_region);
    wl_subsurface_place_above(surface->subsurface, surface->env->root_environment_subsurface->surface);
    surface->drag_pointer = pointer;
    pointer->grabbed_surface = surface;

    active_pointer.temp_dx = active_pointer.x;
    active_pointer.temp_dy = active_pointer.y;
    active_pointer.last_tick = 0;

    surface->env->pending_commit = true;
}
void environment_subsurface_release(environment_subsurface_t* surface) {
    if (!surface) return;
    if (!surface->surface) return;
    if (!surface->pose) return;
    if (!surface->env) return;
    surface->is_grabbed = false;
    layer_surface_enable_input(surface->env->root_surface, false);
    wl_surface_set_input_region(surface->surface, surface->pose->sprite[surface->mascot->LookingRight->value.i]->input_region);
    wl_subsurface_place_below(surface->subsurface, surface->env->root_environment_subsurface->surface);

    if (surface->drag_pointer) {
        surface->drag_pointer->grabbed_surface = NULL;
    }

    surface->env->pending_commit = true;
    surface->drag_pointer = NULL;
}
bool environment_subsurface_move_to_pointer(environment_subsurface_t* surface, uint32_t tick) {
    if (!surface) return false;
    if (!surface->surface) return false;
    if (!surface->pose) return false;
    if (!surface->env) return false;
    if (!surface->drag_pointer) return false;
    if (!surface->is_grabbed) return false;

    if (active_pointer.last_tick+1 < tick) {
    // if (1) {
        active_pointer.temp_dx = active_pointer.x;
        active_pointer.temp_dy = active_pointer.y;
        active_pointer.last_tick = tick;
    }

    environment_subsurface_set_position(surface, surface->drag_pointer->x, surface->drag_pointer->y);
    if (surface->mascot) mascot_moved(surface->mascot, surface->drag_pointer->x, yconv(surface->env, surface->drag_pointer->y));
    return true;
}

uint32_t environment_cursor_x(environment_t* env) {
    UNUSED(env);
    return active_pointer.x;
}

uint32_t environment_cursor_y(environment_t* env) {
    UNUSED(env);
    return active_pointer.y;
}

int32_t environment_cursor_dx(environment_t* env) {
    UNUSED(env);
    return active_pointer.dx/2;
}

int32_t environment_cursor_dy(environment_t* env) {
    UNUSED(env);
    return -active_pointer.dy;
}


uint32_t environment_cursor_get_tick_diff(environment_pointer_t* pointer, uint32_t tick)
{
    return tick - pointer->last_tick;
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
        environment_dispatch();
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
    INFO("Associating mascot %p with surface %p", mascot_ptr, surface);
    surface->mascot = mascot_ptr;
}

void environment_subsurface_set_offset(environment_subsurface_t* surface, int32_t x, int32_t y) {
    if (!surface) return;
    surface->offset_x = x;
    surface->offset_y = y;

}

void environment_select_position(void (*callback)(environment_t*, int32_t, int32_t, environment_subsurface_t*, void*), void* data)
{
    active_pointer.select_callback = callback;
    active_pointer.select_active = true;
    active_pointer.select_data = data;
    active_pointer.cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
    if (active_pointer.cursor_shape_device && active_pointer.pointer) {
        wp_cursor_shape_device_v1_set_shape(active_pointer.cursor_shape_device, active_pointer.enter_serial, active_pointer.cursor_shape);
    }
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
    INFO("get_mascot %p -> %p", surface, surface->mascot);
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
