/*
    layer_surface.h - wl_shimeji's overlay functionality

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

#ifndef LAYER_SURFACE_H
#define LAYER_SURFACE_H

#include "master_header.h"

#define LAYER_TYPE_OVERLAY ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
#define LAYER_TYPE_TOP ZWLR_LAYER_SHELL_V1_LAYER_TOP
#define LAYER_TYPE_BOTTOM ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM
#define LAYER_TYPE_BACKGROUND ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND


#include "wayland_includes.h"

struct layer_surface
{
    struct wl_output* output;

    struct wl_buffer* buffer;
    struct wl_region* region;

    struct wl_surface* surface;

    uint32_t configure_serial;

    uint32_t width;
    uint32_t height;

    void (*resolution_callback)(uint32_t, uint32_t, void*);
    void* resolution_data;

    void (*closed_callback)(void*);
    void* closed_data;

    struct zwlr_layer_surface_v1* layer_surface;
};

struct layer_surface* layer_surface_create(struct wl_output* output, uint32_t layer);
void layer_surface_destroy(struct layer_surface* _layer_surface);

struct wl_subsurface* layer_surface_attach_subsurface(struct layer_surface* _layer_surface, struct wl_surface* surface, int32_t x, int32_t y);
void layer_surface_commit(struct layer_surface* _layer_surface);

void layer_surface_map(struct layer_surface* _layer_surface);
void layer_surface_unmap(struct layer_surface* _layer_surface);

void layer_surface_enable_input(struct layer_surface* _layer_surface, bool enable);
void layer_surface_set_dimensions_callback(struct layer_surface* _layer_surface, void (*callback)(uint32_t, uint32_t, void*), void* data);
void layer_surface_set_closed_callback(struct layer_surface* _layer_surface, void (*callback)(void*), void* data);
#endif
