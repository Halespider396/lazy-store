#include <stdlib.h>
#include <string.h>
#include "package.h"

void pkglist_init(PackageList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

Package *pkglist_push(PackageList *list) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 256 : list->capacity * 2;
        Package *grown = realloc(list->items, new_cap * sizeof(Package));
        if (!grown) return NULL;
        list->items = grown;
        list->capacity = new_cap;
    }
    Package *p = &list->items[list->count++];
    memset(p, 0, sizeof(Package));
    return p;
}

void pkglist_free(PackageList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
