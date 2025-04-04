/*
    mascot_atlas.h - wl_shimeji's mascot texture atlas system

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

#ifndef MASCOT_ATLAS
#define MASCOT_ATLAS

#include <stdint.h>

struct mascot_sprite;
struct mascot_atlas;

#include "master_header.h"
#include "environment.h"

#define MASCOT_ENOSPRITE 1

struct mascot_sprite {
    environment_buffer_t* buffer;
    uint64_t offset;
    uint32_t height, width;
    struct {
        uint32_t x, y;
        uint32_t w, h;
    } ireg;
};

struct mascot_atlas {
    struct mascot_sprite* sprites; // Array has following layout: buffers[x] -> left image; buffers[x+1] -> right image
    uint16_t sprite_count;
    char ** name_order;
};

struct mascot_atlas* mascot_atlas_new(const char* dirname);
void mascot_atlas_destroy(struct mascot_atlas* atlas);

struct mascot_sprite* mascot_atlas_get(const struct mascot_atlas* atlas, uint16_t index, bool right);
uint16_t mascot_atlas_get_name_index(const struct mascot_atlas* atlas, const char* name);

#endif
