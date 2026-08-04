#pragma once
#include <bielik/bk_alloc.h>

/// True if a is nullptr, all-zero, or has all three functions set -- the two
/// valid shapes. A partially-set struct is invalid everywhere.
bool bk__allocator_valid(const BK_Allocator *a);

/// Installs the app-wide base allocator (what a == nullptr resolves to).
/// Validates all-or-nothing; returns false and logs on a partial struct.
/// nullptr or all-zero resets to the SDL-heap default. Called once from
/// bk__boot before SDL_Init; tests call it directly.
bool bk__alloc_install(const BK_Allocator *base);

// Framework-internal allocation: the public call layer plus a tag and an
// allocator choice. a == nullptr (or all-zero *a) means the installed base
// allocator. Same OOM-aborts, sized-free contract as the public layer.
void *bk__alloc(const BK_Allocator *a, BK_MemTag tag, isize size, bool zero, const char *file,
                int line);
void *bk__realloc(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize old_size, isize new_size,
                  const char *file, int line);
void bk__free(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize size);

#define BK__ALLOC(a, tag, size)      bk__alloc((a), (tag), (size), false, __FILE__, __LINE__)
#define BK__ALLOC_ZERO(a, tag, size) bk__alloc((a), (tag), (size), true, __FILE__, __LINE__)
#define BK__REALLOC(a, tag, p, old, new) \
  bk__realloc((a), (tag), (p), (old), (new), __FILE__, __LINE__)
#define BK__FREE(a, tag, p, size) bk__free((a), (tag), (p), (size))
