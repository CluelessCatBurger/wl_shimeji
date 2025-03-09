/*
    mascot_config_parser.h - wl_shimeji's mascot configuration parser

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


#ifndef MASCOT_CONFIG_PARSER_H
#define MASCOT_CONFIG_PARSER_H

typedef struct mascot_prototype_store_ mascot_prototype_store;

#include "mascot.h"

enum mascot_prototype_load_result {
    PROTOTYPE_LOAD_SUCCESS,
    PROTOTYPE_LOAD_NOT_FOUND,
    PROTOTYPE_LOAD_NOT_DIRECTORY,
    PROTOTYPE_LOAD_PERMISSION_DENIED,
    PROTOTYPE_LOAD_UNKNOWN_ERROR,
    PROTOTYPE_LOAD_MANIFEST_NOT_FOUND,
    PROTOTYPE_LOAD_MANIFEST_INVALID,
    PROTOTYPE_LOAD_ACTIONS_NOT_FOUND,
    PROTOTYPE_LOAD_ACTIONS_INVALID,
    PROTOTYPE_LOAD_BEHAVIORS_NOT_FOUND,
    PROTOTYPE_LOAD_BEHAVIORS_INVALID,
    PROTOTYPE_LOAD_PROGRAMS_NOT_FOUND,
    PROTOTYPE_LOAD_PROGRAMS_INVALID,
    PROTOTYPE_LOAD_ASSETS_FAILED,
    PROTOTYPE_LOAD_ALREADY_LOADED,
    PROTOTYPE_LOAD_ENV_NOT_READY,
    PROTOTYPE_LOAD_NULL_POINTER,

    PROTOTYPE_LOAD_VERSION_TOO_OLD,
    PROTOTYPE_LOAD_VERSION_TOO_NEW,

    PROTOTYPE_LOAD_OOM,
};

struct mascot_prototype* mascot_prototype_new();
void mascot_prototype_link(const struct mascot_prototype*);
void mascot_prototype_unlink(const struct mascot_prototype*);
enum mascot_prototype_load_result mascot_prototype_load(struct mascot_prototype*, const char* prototypes_root, const char* path);

mascot_prototype_store* mascot_prototype_store_new();
bool mascot_prototype_store_add(mascot_prototype_store*, const struct mascot_prototype*);
bool mascot_prototype_store_remove(mascot_prototype_store*, const struct mascot_prototype*);
struct mascot_prototype* mascot_prototype_store_get(mascot_prototype_store*, const char* name);
struct mascot_prototype* mascot_prototype_store_get_by_id(mascot_prototype_store*, uint32_t id);
struct mascot_prototype* mascot_prototype_store_get_index(mascot_prototype_store*, int index);
int mascot_prototype_store_count(mascot_prototype_store*);
void mascot_prototype_store_free(mascot_prototype_store*);

void mascot_prototype_store_set_location(mascot_prototype_store* store, const char* path);
int32_t mascot_prototype_store_get_fd(mascot_prototype_store* store);
uint32_t mascot_prototype_store_reload(mascot_prototype_store* store);

#endif
