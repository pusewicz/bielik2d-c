#include "internal/bk_app_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <stddef.h>

#define BK_STR_(x) #x
#define BK_STR(x) BK_STR_(x)

const char *bk_version_string(void) {
    return BK_STR(BK_VERSION_MAJOR) "." BK_STR(BK_VERSION_MINOR) "." BK_STR(BK_VERSION_PATCH);
}

void *bk__alloc(size_t size) { return SDL_malloc(size); }

void *bk__realloc(void *ptr, size_t size) { return SDL_realloc(ptr, size); }

void bk__free(void *ptr) { SDL_free(ptr); }

static constexpr size_t BK_ARENA_DEFAULT_CAPACITY = 4 * 1024 * 1024;

static struct {
    unsigned char *base;
    size_t capacity;
    size_t used;
} s_frame_arena;

void bk__arena_reset(void) { s_frame_arena.used = 0; }

void *bk_frame_alloc(size_t size, size_t align) {
    if (s_frame_arena.base == NULL) {
        unsigned char *base = bk__alloc(BK_ARENA_DEFAULT_CAPACITY);
        BK_ASSERT(base != NULL);
        if (base == NULL) {
            return NULL;
        }
        s_frame_arena.base = base;
        s_frame_arena.capacity = BK_ARENA_DEFAULT_CAPACITY;
        s_frame_arena.used = 0;
    }

    if (align == 0) {
        align = alignof(max_align_t);
    } else {
        BK_ASSERT((align & (align - 1)) == 0);
    }

    size_t worst_case = s_frame_arena.used + (align - 1) + size;
    if (worst_case > s_frame_arena.capacity) {
        size_t new_capacity = s_frame_arena.capacity;
        while (new_capacity < worst_case) {
            new_capacity *= 2;
        }
        unsigned char *new_base = bk__realloc(s_frame_arena.base, new_capacity);
        if (new_base == NULL) {
            BK_ASSERT(false);
            return NULL;
        }
        s_frame_arena.base = new_base;
        s_frame_arena.capacity = new_capacity;
        SDL_Log("BK: frame arena grew to %zu bytes", new_capacity);
    }

    uintptr_t base_addr = (uintptr_t)s_frame_arena.base;
    uintptr_t cursor_addr = base_addr + s_frame_arena.used;
    uintptr_t aligned_addr = (cursor_addr + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t aligned_offset = (size_t)(aligned_addr - base_addr);

    s_frame_arena.used = aligned_offset + size;
    return s_frame_arena.base + aligned_offset;
}
