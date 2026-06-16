/**
 * cls.c - Coroutine-Local Storage (CLS) implementation
 */

#include "cls.h"
#include "../coco_internal.h"
#include <stdlib.h>
#include <string.h>

static cls_entry_t *find_entry(cls_entry_t *table, const char *key) {
    while (table) {
        if (strcmp(table->key, key) == 0) {
            return table;
        }
        table = table->next;
    }
    return NULL;
}

cls_entry_t *cls_create_table(void) {
    return NULL; /* Empty list */
}

void cls_destroy_table(cls_entry_t *table) {
    while (table) {
        cls_entry_t *next = table->next;
        if (table->destructor && table->value) {
            table->destructor(table->value);
        }
        free(table);
        table = next;
    }
}

int cls_set(cls_entry_t **table, const char *key, void *value, void (*destructor)(void *)) {
    if (!table || !key) return -1;
    
    /* Check if key already exists */
    cls_entry_t *existing = find_entry(*table, key);
    if (existing) {
        /* Call destructor for old value */
        if (existing->destructor && existing->value) {
            existing->destructor(existing->value);
        }
        existing->value = value;
        existing->destructor = destructor;
        return 0;
    }
    
    /* Create new entry */
    cls_entry_t *entry = malloc(sizeof(cls_entry_t));
    if (!entry) return -1;
    
    entry->key = key;
    entry->value = value;
    entry->destructor = destructor;
    entry->next = *table;
    *table = entry;
    
    return 0;
}

void *cls_get(cls_entry_t *table, const char *key) {
    if (!key) return NULL;
    cls_entry_t *entry = find_entry(table, key);
    return entry ? entry->value : NULL;
}

int cls_delete(cls_entry_t **table, const char *key) {
    if (!table || !key) return -1;
    
    cls_entry_t **current = table;
    while (*current) {
        if (strcmp((*current)->key, key) == 0) {
            cls_entry_t *to_delete = *current;
            *current = to_delete->next;
            
            if (to_delete->destructor && to_delete->value) {
                to_delete->destructor(to_delete->value);
            }
            free(to_delete);
            return 0;
        }
        current = &(*current)->next;
    }
    
    return -1; /* Not found */
}

/* Public API */
int coco_cls_set(const char *key, void *value, void (*destructor)(void *)) {
    ENSURE_IN_CORO();
    coco_coro_t *coro = g_current_coro;
    if (!coro) return COCO_ERROR_INVALID;
    
    if (cls_set(&coro->cls_table, key, value, destructor) != 0) {
        return COCO_ERROR_NOMEM;
    }
    return COCO_OK;
}

void *coco_cls_get(const char *key) {
    ENSURE_IN_CORO_RET(NULL);
    coco_coro_t *coro = g_current_coro;
    if (!coro) return NULL;
    
    return cls_get(coro->cls_table, key);
}

int coco_cls_delete(const char *key) {
    ENSURE_IN_CORO();
    coco_coro_t *coro = g_current_coro;
    if (!coro) return COCO_ERROR_INVALID;
    
    if (cls_delete(&coro->cls_table, key) != 0) {
        return COCO_ERROR;
    }
    return COCO_OK;
}
