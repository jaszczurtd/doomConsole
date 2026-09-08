#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef DOOM_COLUMN_CACHE_CAPACITY
#define DOOM_COLUMN_CACHE_CAPACITY 255
#endif
#ifndef HAL_PATCH_COLUMN_CACHE_HASH_SIZE
#define HAL_PATCH_COLUMN_CACHE_HASH_SIZE 512
#endif
#include "jaszczurhal/doom_column_cache.h"

/* Reference: the previous hash hint, full lookup and oldest-age scan. */
typedef struct {
    int32_t lump[DOOM_COLUMN_CACHE_CAPACITY];
    uint16_t column[DOOM_COLUMN_CACHE_CAPACITY];
    uint32_t age[DOOM_COLUMN_CACHE_CAPACITY];
    uint8_t hint[HAL_PATCH_COLUMN_CACHE_HASH_SIZE];
    unsigned count;
    uint32_t clock;
} reference_cache_t;

static int reference_find(reference_cache_t *cache, int32_t lump,
                          uint16_t column)
{
    ++cache->clock;
    const unsigned hash = DoomColumnCache_Hash(lump, column);
    const unsigned hinted = cache->hint[hash];
    if (hinted != 0u && cache->lump[hinted - 1u] == lump &&
        cache->column[hinted - 1u] == column) {
        cache->age[hinted - 1u] = cache->clock;
        return (int)hinted - 1;
    }
    for (unsigned i = 0; i < cache->count; ++i) {
        if (cache->lump[i] == lump && cache->column[i] == column) {
            cache->age[i] = cache->clock;
            cache->hint[hash] = (uint8_t)(i + 1u);
            return (int)i;
        }
    }
    return -1;
}

static unsigned reference_insert(reference_cache_t *cache, unsigned capacity,
                                  int32_t lump, uint16_t column)
{
    unsigned slot = 0u;
    if (cache->count < capacity) {
        slot = cache->count++;
    } else {
        for (unsigned i = 1u; i < capacity; ++i) {
            if (cache->age[i] < cache->age[slot]) {
                slot = i;
            }
        }
    }
    cache->lump[slot] = lump;
    cache->column[slot] = column;
    cache->age[slot] = cache->clock;
    cache->hint[DoomColumnCache_Hash(lump, column)] = (uint8_t)(slot + 1u);
    return slot;
}

static uint32_t random_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void check_access(doom_column_cache_t *cache, reference_cache_t *ref,
                          unsigned capacity, int32_t lump, uint16_t column,
                          bool decode_ok)
{
    int slot = DoomColumnCache_Find(cache, lump, column);
    const int expected = reference_find(ref, lump, column);
    assert(slot == expected);
    if (slot < 0 && decode_ok) {
        slot = (int)DoomColumnCache_Insert(cache, capacity, lump, column);
        assert((unsigned)slot == reference_insert(ref, capacity, lump, column));
    }
    if (slot >= 0) {
        assert((unsigned)slot < capacity);
        assert(cache->lump[slot] == lump);
        assert(cache->column[slot] == column);
    }
}

static void test_sequences(unsigned capacity)
{
    doom_column_cache_t cache[2] = {0};
    reference_cache_t ref[2] = {0};
    uint32_t random = 1u;

    /* Interleaved owners, with different capacities (an uneven core split). */
    for (unsigned i = 0; i < 100000u; ++i) {
        const uint32_t value = random_next(&random);
        const unsigned core = (value >> 16) & 1u;
        const unsigned slots = core == 0u ? capacity : (capacity + 1u) / 2u;
        const int32_t lump = (int32_t)((value >> 8) % 6u) - 3;
        const uint16_t column = (uint16_t)((value >> 20) % (capacity + 7u));
        check_access(&cache[core], &ref[core], slots, lump, column, i % 11u != 0u);
        if (i % 3u == 0u) {
            check_access(&cache[core], &ref[core], slots, lump, column, true);
        }
    }
}

static void test_collisions(void)
{
    doom_column_cache_t cache = {0};
    reference_cache_t ref = {0};
    int32_t lumps[5];
    unsigned found = 0u;
    for (int32_t lump = -10000; found < 5u; ++lump) {
        if (DoomColumnCache_Hash(lump, UINT16_MAX) == 0u) {
            lumps[found++] = lump;
        }
    }

    /* Repeated hits through collisions, then evict from different chain
     * positions while a miss without insertion leaves the pixels intact. */
    for (unsigned i = 0; i < 3u; ++i) {
        check_access(&cache, &ref, 3u, lumps[i], UINT16_MAX, true);
    }
    check_access(&cache, &ref, 3u, lumps[3], UINT16_MAX, false);
    for (unsigned i = 0; i < 100u; ++i) {
        check_access(&cache, &ref, 3u, lumps[(i * 3u) % 5u], UINT16_MAX, true);
        check_access(&cache, &ref, 3u, lumps[(i + 1u) % 5u], UINT16_MAX, true);
    }
    check_access(&cache, &ref, 3u, INT32_MIN, 0u, true);
    check_access(&cache, &ref, 3u, INT32_MAX, UINT16_MAX, true);
}

static void benchmark(void)
{
    doom_column_cache_t cache = {0};
    reference_cache_t ref = {0};
    uint64_t sums[2] = {0};
    double elapsed[2];
    const unsigned iterations = 1000000u;

    for (unsigned variant = 0; variant < 2u; ++variant) {
        uint32_t random = 1u;
        const clock_t start = clock();
        for (unsigned i = 0; i < iterations; ++i) {
            const uint32_t value = random_next(&random);
            const int32_t lump = (int32_t)((value >> 8) % 6u) - 3;
            const uint16_t column = (uint16_t)((value >> 20) & 127u);
            int slot = variant == 0u
                ? reference_find(&ref, lump, column)
                : DoomColumnCache_Find(&cache, lump, column);
            if (slot < 0) {
                slot = (int)(variant == 0u
                    ? reference_insert(&ref, 224u, lump, column)
                    : DoomColumnCache_Insert(&cache, 224u, lump, column));
            }
            sums[variant] += (unsigned)slot;
        }
        elapsed[variant] = (double)(clock() - start) / CLOCKS_PER_SEC;
    }
    assert(sums[0] == sums[1]);
    printf("Column cache, %u accesses: scan %.6fs, indexed %.6fs (%.2fx)\n",
           iterations, elapsed[0], elapsed[1], elapsed[0] / elapsed[1]);
}

int main(int argc, char **argv)
{
    test_sequences(1u);
    test_sequences(16u);
    test_sequences(84u);
    test_sequences(224u);
    test_sequences(255u);
    test_sequences(DOOM_COLUMN_CACHE_CAPACITY);
    test_collisions();
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) {
        benchmark();
    }
    return 0;
}
