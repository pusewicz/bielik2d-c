#pragma once
#include <bielik/bk_types.h>

// ---------------------------------------------------------------------------
// BK_Allocator -- pluggable allocation for persistent (non-frame) memory.
//
// Not for per-frame scratch: that is bk_frame_alloc (bk_app.h), which is also
// the only aligned-allocation path. This header is deliberately SDL-free.
//
// SDL already ships SDL_SetMemoryFunctions/SDL_GetMemoryFunctions, but that is
// one process-wide swap of the C malloc/calloc/realloc/free quartet with no
// ctx parameter and no allocation size passed back to free/realloc -- it
// can't express a per-instance allocator (e.g. one arena per subsystem) or
// the size-aware free that a bump/pool allocator needs. BK_Allocator is a
// different shape for a different job; wiring bk_alloc's default heap path
// through SDL's functions instead of straight to SDL_malloc/SDL_free is a
// later task, not this one.
// ---------------------------------------------------------------------------

/// A pluggable allocator. All-zero means: use the framework default (SDL heap).
/// If any function is set, all three must be set -- partial override is
/// rejected wherever an allocator is installed. free_fn receives the original
/// allocation size and realloc_fn both sizes, so implementations never need
/// per-allocation headers. Returned memory is uninitialized (zeroing is the
/// call layer's job) and must be aligned for max_align_t. Returning nullptr
/// means failure; the framework logs and aborts. Thread-safety is the
/// implementation's concern: the base allocator (BK_AppDesc) must be
/// thread-safe if app callbacks allocate off the main thread.
typedef struct BK_Allocator {
  void *(*alloc_fn)(isize size, void *ctx);
  void *(*realloc_fn)(void *ptr, isize old_size, isize new_size, void *ctx);
  void (*free_fn)(void *ptr, isize size, void *ctx);
  void *ctx;
} BK_Allocator;

// Call-layer implementation functions. Use the macros below instead: they
// capture __FILE__/__LINE__ for OOM messages (and a future site table). The
// file/line never reach BK_Allocator implementations.
void *bk__alloc_site(const BK_Allocator *a, isize size, bool zero, const char *file, int line);
void *bk__realloc_site(const BK_Allocator *a, void *ptr, isize old_size, isize new_size,
                       const char *file, int line);
void bk__free_site(const BK_Allocator *a, void *ptr, isize size);

/// Allocates size bytes, uninitialized. a == nullptr (or all-zero *a) uses the
/// framework default allocator. Never returns nullptr: allocation failure logs
/// and aborts. size must be >= 0.
#define bk_alloc(a, size) bk__alloc_site((a), (size), false, __FILE__, __LINE__)

/// Like bk_alloc, but the returned bytes are zeroed.
#define bk_alloc_zero(a, size) bk__alloc_site((a), (size), true, __FILE__, __LINE__)

/// Resizes ptr from old_size to new_size, preserving min(old,new) bytes.
/// ptr may be nullptr iff old_size == 0 (then it is an allocation).
#define bk_realloc(a, ptr, old_size, new_size) \
  bk__realloc_site((a), (ptr), (old_size), (new_size), __FILE__, __LINE__)

/// Frees ptr; size must equal the allocation's current size. nullptr: no-op.
#define bk_free(a, ptr, size) bk__free_site((a), (ptr), (size))

/// Typed, zeroed single-object / array allocation.
#define BK_NEW(a, T)          ((T *)bk_alloc_zero((a), (isize)sizeof(T)))
#define BK_NEW_ARRAY(a, T, n) ((T *)bk_alloc_zero((a), (isize)sizeof(T) * (n)))
