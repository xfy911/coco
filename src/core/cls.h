/**
 * cls.h - Coroutine-Local Storage (CLS) internal API
 */

#ifndef CLS_H
#define CLS_H

#include <stddef.h>

/* CLS entry */
typedef struct cls_entry {
    const char *key;
    void *value;
    void (*destructor)(void *);
    struct cls_entry *next;
} cls_entry_t;

/* CLS operations */
cls_entry_t *cls_create_table(void);
void cls_destroy_table(cls_entry_t *table);
int cls_set(cls_entry_t **table, const char *key, void *value, void (*destructor)(void *));
void *cls_get(cls_entry_t *table, const char *key);
int cls_delete(cls_entry_t **table, const char *key);

#endif /* CLS_H */
