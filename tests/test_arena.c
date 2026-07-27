#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include <bielik/bk_app.h>
#include <stdint.h>
#include <string.h>

static void test_alignment_across_powers_of_two(void) {
    const size_t aligns[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    for (size_t i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        bk__arena_reset();
        void *ptr = bk_frame_alloc(37, aligns[i]);
        REQUIRE(ptr != NULL);
        REQUIRE(((uintptr_t)ptr % aligns[i]) == 0);
    }
}

static void test_reset_rewinds_cursor(void) {
    bk__arena_reset();
    void *p1 = bk_frame_alloc(64, 0);
    bk__arena_reset();
    void *p2 = bk_frame_alloc(64, 0);
    REQUIRE(p1 == p2);
}

static void test_growth_preserves_contents_and_stays_aligned(void) {
    bk__arena_reset();

    unsigned char *marker_before_growth = (unsigned char *)bk_frame_alloc(256, 0);
    REQUIRE(marker_before_growth != NULL);
    memset(marker_before_growth, 0xAB, 256);

    const size_t chunk_size = 64 * 1024;
    const int chunk_count = 90;
    for (int i = 0; i < chunk_count; i++) {
        void *ptr = bk_frame_alloc(chunk_size, 0);
        REQUIRE(ptr != NULL);
        REQUIRE(((uintptr_t)ptr % alignof(max_align_t)) == 0);
    }

    bk__arena_reset();
    unsigned char *marker_after_growth = (unsigned char *)bk_frame_alloc(256, 0);
    REQUIRE(marker_after_growth != NULL);
    for (size_t i = 0; i < 256; i++) {
        REQUIRE(marker_after_growth[i] == 0xAB);
    }

    const size_t aligns[] = {8, 16, 32, 64, 128, 256};
    for (size_t i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        void *ptr = bk_frame_alloc(97, aligns[i]);
        REQUIRE(ptr != NULL);
        REQUIRE(((uintptr_t)ptr % aligns[i]) == 0);
    }
}

static void test_growth_with_large_align(void) {
    bk__arena_reset();
    void *big = bk_frame_alloc(9u * 1024 * 1024, 4096);
    REQUIRE(big != NULL);
    REQUIRE(((uintptr_t)big % 4096) == 0);
}

static void test_interleaved_allocations_do_not_overlap(void) {
    bk__arena_reset();

    enum { CHUNK_COUNT = 24 };
    static const size_t sizes[] = {1, 3, 7, 16, 33, 100, 5};
    static const size_t aligns[] = {1, 4, 8, 16, 32};

    unsigned char *ptrs[CHUNK_COUNT];
    size_t chunk_sizes[CHUNK_COUNT];
    uint8_t patterns[CHUNK_COUNT];

    for (int i = 0; i < CHUNK_COUNT; i++) {
        size_t size = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        size_t align = aligns[i % (sizeof(aligns) / sizeof(aligns[0]))];
        unsigned char *ptr = (unsigned char *)bk_frame_alloc(size, align);
        REQUIRE(ptr != NULL);
        REQUIRE(((uintptr_t)ptr % align) == 0);

        uint8_t pattern = (uint8_t)(i + 1);
        memset(ptr, pattern, size);

        ptrs[i] = ptr;
        chunk_sizes[i] = size;
        patterns[i] = pattern;
    }

    for (int i = 0; i < CHUNK_COUNT; i++) {
        for (size_t b = 0; b < chunk_sizes[i]; b++) {
            REQUIRE(ptrs[i][b] == patterns[i]);
        }
    }
}

int main(void) {
    test_alignment_across_powers_of_two();
    test_reset_rewinds_cursor();
    test_growth_preserves_contents_and_stays_aligned();
    test_growth_with_large_align();
    test_interleaved_allocations_do_not_overlap();
    printf("test_arena: OK\n");
    return 0;
}
