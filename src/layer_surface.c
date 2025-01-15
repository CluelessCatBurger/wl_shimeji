/*
    layer_surface.c - wl_shimeji's overlay functionality

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
#include <sys/mman.h>
#include <unistd.h>

#include "layer_surface.h"
#include <assert.h>

extern struct wl_display* display;
extern struct wl_compositor* compositor;
extern struct wl_subcompositor* subcompositor;
extern struct wl_shm* shm_manager;
extern struct zwlr_layer_shell_v1* wlr_layer_shell;

static void configure(void* data, struct zwlr_layer_surface_v1* wlr_layer_surface, uint32_t serial, uint32_t width, uint32_t height)
{

    struct layer_surface* _layer_surface = (struct layer_surface*)data;

    zwlr_layer_surface_v1_ack_configure(wlr_layer_surface, serial);

    _layer_surface->width = width;
    _layer_surface->height = height;

    _layer_surface->configure_serial = serial;

    if (_layer_surface->buffer) wl_buffer_destroy(_layer_surface->buffer);

    int memfd = memfd_create("layer_surface", 0);
    assert(memfd >= 0);

    ftruncate(memfd, width*height*4);

    struct wl_shm_pool* shm_pool = wl_shm_create_pool(shm_manager, memfd, height*width*4);
    _layer_surface->buffer = wl_shm_pool_create_buffer(shm_pool, 0, width, height, width*4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(shm_pool);

    close(memfd);

    wl_surface_attach(_layer_surface->surface, _layer_surface->buffer, 0, 0);
    wl_surface_set_input_region(_layer_surface->surface, _layer_surface->region);
    wl_surface_damage(_layer_surface->surface, 0, 0, width, height);

    wl_surface_commit(_layer_surface->surface);

    if (_layer_surface->resolution_callback) _layer_surface->resolution_callback(width, height, _layer_surface->resolution_data);

}

static void closed(void* data, struct zwlr_layer_surface_v1* wlr_layer_surface)
{
    UNUSED(wlr_layer_surface);
    struct layer_surface* _layer_surface = (struct layer_surface*)data;
    if (_layer_surface->closed_callback) _layer_surface->closed_callback(_layer_surface->closed_data);
}

const struct zwlr_layer_surface_v1_listener wlr_shell_surface_listener = {
    .closed = closed,
    .configure = configure
};


struct layer_surface* layer_surface_create(struct wl_output* output, uint32_t layer)
{
    struct layer_surface* _layer_surface = calloc(1, sizeof(struct layer_surface));
    if (!_layer_surface) return NULL;

    _layer_surface->output = output;
    _layer_surface->region = wl_compositor_create_region(compositor);
    _layer_surface->surface = wl_compositor_create_surface(compositor);

    _layer_surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(wlr_layer_shell, _layer_surface->surface, _layer_surface->output, layer, "wl_shimeji");

    zwlr_layer_surface_v1_add_listener(_layer_surface->layer_surface, &wlr_shell_surface_listener, (void*)_layer_surface);

    return _layer_surface;
}

void layer_surface_destroy(struct layer_surface* _layer_surface)
{
    if (!_layer_surface) return;

    if (_layer_surface->layer_surface) zwlr_layer_surface_v1_destroy(_layer_surface->layer_surface);
    if (_layer_surface->surface) wl_surface_destroy(_layer_surface->surface);
    if (_layer_surface->buffer) wl_buffer_destroy(_layer_surface->buffer);
    if (_layer_surface->region) wl_region_destroy(_layer_surface->region);

    free(_layer_surface);
}

void layer_surface_map(struct layer_surface* _layer_surface)
{
    assert(_layer_surface);

    if (_layer_surface->configure_serial) return;

    zwlr_layer_surface_v1_set_anchor(_layer_surface->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(_layer_surface->layer_surface, 0, 0);

    wl_surface_commit(_layer_surface->surface);

}

void layer_surface_enable_input(struct layer_surface *_layer_surface, bool enable)
{
    assert(_layer_surface);

    if (!_layer_surface->configure_serial) return;

    if (enable)
    {
        wl_region_add(_layer_surface->region, 0, 0, _layer_surface->width, _layer_surface->height);
        wl_surface_set_input_region(_layer_surface->surface, _layer_surface->region);
    }
    else
    {
        wl_region_subtract(_layer_surface->region, 0, 0, _layer_surface->width, _layer_surface->height);
        wl_surface_set_input_region(_layer_surface->surface, _layer_surface->region);
    }

    wl_surface_commit(_layer_surface->surface);
}

void layer_surface_unmap(struct layer_surface* _layer_surface)
{
    assert(_layer_surface);

    if (!_layer_surface->configure_serial || !_layer_surface->buffer) return;

    wl_surface_attach(_layer_surface->surface, NULL, 0, 0);
    wl_surface_commit(_layer_surface->surface);

    wl_buffer_destroy(_layer_surface->buffer);

    _layer_surface->buffer = NULL;
    _layer_surface->configure_serial = 0;
    _layer_surface->width = 0;
    _layer_surface->height = 0;
}

struct wl_subsurface* layer_surface_attach_subsurface(struct layer_surface* _layer_surface, struct wl_surface* surface, int32_t x, int32_t y)
{
    assert(_layer_surface);
    assert(_layer_surface->configure_serial);

    struct wl_subsurface* subsurface = wl_subcompositor_get_subsurface(
        subcompositor, surface, _layer_surface->surface
    );
    wl_subsurface_set_desync(subsurface);
    wl_subsurface_set_position(subsurface, x, y);

    wl_surface_commit(_layer_surface->surface);

    return subsurface;
}

void layer_surface_commit(struct layer_surface* _layer_surface)
{
    assert(_layer_surface);
    wl_surface_commit(_layer_surface->surface);
}

void layer_surface_set_dimensions_callback(struct layer_surface* _layer_surface, void (*callback)(uint32_t, uint32_t, void*), void* data)
{
    assert(_layer_surface);

    _layer_surface->resolution_callback = callback;
    _layer_surface->resolution_data = data;
}

void layer_surface_set_closed_callback(struct layer_surface* _layer_surface, void (*callback)(void*), void* data)
{
    assert(_layer_surface);

    _layer_surface->closed_callback = callback;
    _layer_surface->closed_data = data;
}
