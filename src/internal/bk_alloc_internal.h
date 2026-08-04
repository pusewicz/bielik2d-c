#pragma once
#include <bielik/bk_alloc.h>

/// True if a is nullptr, all-zero, or has all three functions set -- the two
/// valid shapes. A partially-set struct is invalid everywhere.
bool bk__allocator_valid(const BK_Allocator *a);

/// Installs the app-wide base allocator (what a == nullptr resolves to).
/// Validates all-or-nothing; returns false and logs on a partial struct.
/// nullptr or all-zero resets to the libc-heap default. Called once from
/// bk__boot before SDL_Init; tests call it directly.
bool bk__alloc_install(const BK_Allocator *base);

/// Routes SDL's internal allocations through the installed base allocator via
/// SDL_SetMemoryFunctions. Call after bk__alloc_install and before SDL_Init,
/// only. Returns false (already logged where warranted) when routing is
/// skipped: default-heap base, pre-boot SDL allocations, or SDL refusal.
///
/// "Pre-boot SDL allocations" is a live SDL_GetNumAllocations() check, not an
/// assumption: any SDL call an embedder makes before bk_run (SDL_SetHint is the
/// idiomatic one, and get_desc() runs before this call too) produces pointers
/// with no size header, which the shim would later misread as it frees them.
/// Where SDL was built without SDL_TRACK_ALLOCATION_COUNT the count is -1 and
/// routing proceeds on the assumption that nothing has allocated.
///
/// Consequence worth knowing: a second bk_run in the same process never routes.
/// SDL retains allocations past SDL_Quit, so the count is nonzero by then --
/// and that is the correct answer, since those retained pointers came from the
/// first run's allocator. Once installed, the base allocator (and its ctx) must
/// outlive the process for the same reason: nothing ever un-routes SDL's memory
/// functions back off the shim.
bool bk__alloc_route_sdl(void);

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
