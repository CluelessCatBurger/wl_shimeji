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
void* list_get_(struct list* list, uint32_t index);
void list_free_(struct list* list);
uint32_t list_count_(struct list* list);
uint32_t list_find_(struct list* list, void* entry);

#endif
