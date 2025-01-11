#include "list.h"

#include <malloc.h>
#include <stdint.h>
#include <string.h>

struct list* list_init_(uint32_t capacity)
{
    struct list* list = malloc(sizeof(struct list));
    list->entry_count = capacity;
    list->entries = calloc(1, sizeof(void*) * capacity);
    list->entry_used = calloc(1, capacity);
    return list;
}

void list_free_(struct list* list)
{
    free(list->entries);
    free(list->entry_used);
    free(list);
}

void* list_get_(struct list* list, uint32_t index)
{
    if (index >= list->entry_count) {
        return NULL;
    }
    if (!list->entry_used[index]) {
        return NULL;
    }
    return list->entries[index];
}

uint32_t list_add_(struct list* list, void* entry)
{
    if (!entry) {
        return UINT32_MAX;
    }
    if (list->occupied == list->entry_count) {
        // Grow the list
        uint32_t new_size = list->entry_count * 2;
        list->entries = realloc(list->entries, sizeof(void*) * new_size);
        list->entry_used = realloc(list->entry_used, new_size);
        memset(list->entry_used + list->entry_count, 0, new_size - list->entry_count);
        list->entry_count = new_size;
    }
    for (uint32_t i = 0; i < list->entry_count; i++) {
        if (!list->entry_used[i]) {
            list->entry_used[i] = 1;
            list->occupied++;
            list->entries[i] = entry;
            return i;
        }
    }
    return UINT32_MAX;
}

void list_remove_(struct list* list, uint32_t index)
{
    if (index >= list->entry_count) {
        return;
    }
    if (!list->entry_used[index]) {
        return;
    }
    list->entry_used[index] = 0;
    list->occupied--;
}

uint32_t list_find_(struct list* list, void* entry)
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

uint32_t list_count_(struct list* list)
{
    return list->occupied;
}
