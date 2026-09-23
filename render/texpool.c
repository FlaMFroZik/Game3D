#include <stdlib.h>
#include <string.h>

#include "render/prim.h"
#include "render/texpool.h"

/* strdup нет в C11 (а в MSVC он называется _strdup), поэтому своя копия. */
static char *dup_string(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

void texpool_init(TexPool *pool) {
    pool->entries = NULL;
    pool->count = 0;
    pool->capacity = 0;
}

void texpool_free(TexPool *pool) {
    if (!pool) return;

    for (size_t i = 0; i < pool->count; i++) {
        TexPoolEntry *entry = pool->entries[i];
        if (!entry) continue;
        prim_free_texture(&entry->tex);
        free(entry->path);
        free(entry);
    }
    free(pool->entries);
    texpool_init(pool);
}

size_t texpool_count(const TexPool *pool) {
    return pool ? pool->count : 0;
}

static TexPoolEntry *find_entry(const TexPool *pool, const char *path) {
    if (!pool || !path) return NULL;

    for (size_t i = 0; i < pool->count; i++) {
        TexPoolEntry *entry = pool->entries[i];
        if (entry && entry->path && strcmp(entry->path, path) == 0) return entry;
    }
    return NULL;
}

const Texture *texpool_find(const TexPool *pool, const char *path) {
    TexPoolEntry *entry = find_entry(pool, path);
    return entry ? &entry->tex : NULL;
}

const Texture *texpool_get(TexPool *pool, const char *path) {
    if (!pool || !path || path[0] == '\0') return NULL;

    TexPoolEntry *known = find_entry(pool, path);
    if (known) return &known->tex;

    if (pool->count == pool->capacity) {
        size_t new_capacity = (pool->capacity == 0) ? 8 : pool->capacity * 2;
        TexPoolEntry **grown = realloc(pool->entries, new_capacity * sizeof(*grown));
        if (!grown) return NULL;
        pool->entries = grown;
        pool->capacity = new_capacity;
    }

    TexPoolEntry *entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;

    entry->path = dup_string(path);
    entry->tex = prim_load_texture(path);

    if (!entry->path || entry->tex.id == 0) {
        free(entry->path);
        free(entry);
        return NULL;
    }

    pool->entries[pool->count++] = entry;
    return &entry->tex;
}
