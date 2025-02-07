/*
    list.h - wl_shimeji's lists

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

#ifndef LIST_H
#define LIST_H

#include "master_header.h"

struct list {
    void **entries;
    uint32_t entry_count;
    uint32_t occupied;
    uint8_t* entry_used;
    pthread_mutex_t mutex;
};

#define list_init(capacity) list_init_(capacity);
#define list_add(list, entry) list_add_((list), (void*)(entry));
#define list_remove(list, index) list_remove_((list), (index));
#define list_get(list, index) (list_get_((list), (index)))
#define list_free(list) list_free_((list))
#define list_count(list) list_count_((list))
#define list_size(list) list->entry_count
#define list_find(list, entry) list_find_((list), (void*)(entry))

struct list* list_init_(uint32_t capacity);
uint32_t list_add_(struct list* list, void* entry);
void list_remove_(struct list* list, uint32_t index);
void list_free_(struct list* list);

static inline void* list_get_(struct list* list, uint32_t index)
{
    if (index >= list->entry_count) {
        return NULL;
    }
    if (!list->entry_used[index]) {
        return NULL;
    }
    return list->entries[index];
}

static inline uint32_t list_find_(struct list* list, void* entry)
{
    if (!entry) {
        return UINT32_MAX;
    }
    for (uint32_t i = 0; i < list->entry_count; i++) {
        if (list->entry_used[i]) {
            if (list->entries[i] == entry) {
                return i;
            }
        }
    }
    return UINT32_MAX;
}

static inline uint32_t list_count_(struct list* list)
{
    return list->occupied;
}

#endif
