#ifndef DOOM_COLUMN_CACHE_H
#define DOOM_COLUMN_CACHE_H

#include <stdint.h>

/* Private decoded-column index. Each renderer core owns one zero-initialized
 * instance. Pixel buffers and their heights stay with the renderer. */
#if DOOM_COLUMN_CACHE_CAPACITY < 1 || DOOM_COLUMN_CACHE_CAPACITY > 65535
#error "DOOM_COLUMN_CACHE_CAPACITY must be in 1..65535 per core."
#endif
#if HAL_PATCH_COLUMN_CACHE_HASH_SIZE < 1 || \
    (HAL_PATCH_COLUMN_CACHE_HASH_SIZE & (HAL_PATCH_COLUMN_CACHE_HASH_SIZE - 1))
#error "HAL_PATCH_COLUMN_CACHE_HASH_SIZE must be a power of two."
#endif

#if DOOM_COLUMN_CACHE_CAPACITY <= 255
typedef uint8_t doom_column_cache_id_t;
#else
typedef uint16_t doom_column_cache_id_t;
#endif

typedef struct {
    int32_t lump[DOOM_COLUMN_CACHE_CAPACITY];
    uint16_t column[DOOM_COLUMN_CACHE_CAPACITY];
    /* Links are one-based; zero ends a chain. */
    doom_column_cache_id_t hash_next[DOOM_COLUMN_CACHE_CAPACITY];
    doom_column_cache_id_t older[DOOM_COLUMN_CACHE_CAPACITY];
    doom_column_cache_id_t newer[DOOM_COLUMN_CACHE_CAPACITY];
    doom_column_cache_id_t bucket[HAL_PATCH_COLUMN_CACHE_HASH_SIZE];
    doom_column_cache_id_t count;
    doom_column_cache_id_t oldest;
    doom_column_cache_id_t newest;
} doom_column_cache_t;

static inline unsigned DoomColumnCache_Hash(int32_t lump, uint16_t column)
{
    uint32_t key = (uint32_t)lump;
    key ^= (uint32_t)column * 40503u;
    key *= 2654435761u;
    key ^= key >> 16;
    return key & (HAL_PATCH_COLUMN_CACHE_HASH_SIZE - 1u);
}

static inline void DoomColumnCache_Touch(doom_column_cache_t *cache,
                                         unsigned slot)
{
    const doom_column_cache_id_t id = (doom_column_cache_id_t)(slot + 1u);
    if (cache->newest == id) {
        return;
    }

    const doom_column_cache_id_t older = cache->older[slot];
    const doom_column_cache_id_t newer = cache->newer[slot];
    if (older != 0u) {
        cache->newer[older - 1u] = newer;
    } else if (cache->oldest == id) {
        cache->oldest = newer;
    }
    if (newer != 0u) {
        cache->older[newer - 1u] = older;
    }

    cache->older[slot] = cache->newest;
    cache->newer[slot] = 0u;
    if (cache->newest != 0u) {
        cache->newer[cache->newest - 1u] = id;
    } else {
        cache->oldest = id;
    }
    cache->newest = id;
}

/* Return a local slot, or -1 on a miss. A hit becomes most recently used. */
static inline int DoomColumnCache_Find(doom_column_cache_t *cache,
                                       int32_t lump, uint16_t column)
{
    doom_column_cache_id_t id = cache->bucket[DoomColumnCache_Hash(lump, column)];
    while (id != 0u) {
        const unsigned slot = id - 1u;
        if (cache->lump[slot] == lump && cache->column[slot] == column) {
            DoomColumnCache_Touch(cache, slot);
            return (int)slot;
        }
        id = cache->hash_next[slot];
    }
    return -1;
}

/* Insert only after a miss and successful decode. Capacity is fixed for the
 * lifetime of this instance, in 1..DOOM_COLUMN_CACHE_CAPACITY. */
static inline unsigned DoomColumnCache_Insert(doom_column_cache_t *cache,
                                               unsigned capacity,
                                               int32_t lump, uint16_t column)
{
    unsigned slot;
    if (cache->count < capacity) {
        slot = cache->count++;
    } else {
        slot = cache->oldest - 1u;
        const unsigned hash =
            DoomColumnCache_Hash(cache->lump[slot], cache->column[slot]);
        doom_column_cache_id_t *link = &cache->bucket[hash];
        while (*link != slot + 1u) {
            link = &cache->hash_next[*link - 1u];
        }
        *link = cache->hash_next[slot];
    }

    const unsigned hash = DoomColumnCache_Hash(lump, column);
    cache->lump[slot] = lump;
    cache->column[slot] = column;
    cache->hash_next[slot] = cache->bucket[hash];
    cache->bucket[hash] = (doom_column_cache_id_t)(slot + 1u);
    DoomColumnCache_Touch(cache, slot);
    return slot;
}

#endif
