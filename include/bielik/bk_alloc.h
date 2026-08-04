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
// different shape for a different job. The framework's own default heap
// (what a == nullptr resolves to) calls libc malloc/realloc/free directly,
// never SDL_malloc/SDL_free: bk_alloc.c's SDL routing (bk__alloc_route_sdl)
// makes SDL_malloc itself dispatch through the installed base allocator, so
// the default heap has to stay a fixed primitive that routing can never loop
// back through -- see DEVIATIONS.md.
// ---------------------------------------------------------------------------

/// A pluggable allocator. All-zero means: use the framework default (the
/// process's libc heap). If any function is set, all three must be set --
/// partial override is rejected wherever an allocator is installed. free_fn
/// receives the original allocation size and realloc_fn both sizes, so
/// implementations never need per-allocation headers. Returned memory is
/// uninitialized (zeroing is the call layer's job) and must be aligned for
/// max_align_t. Returning nullptr means failure; the framework logs and
/// aborts. Thread-safety is the implementation's concern: the base allocator
/// (BK_AppDesc) must be thread-safe if app callbacks allocate off the main
/// thread. Implementations must never call SDL_malloc/SDL_calloc/SDL_realloc/
/// SDL_free (libc or anything else not routed through SDL is fine): once SDL
/// routing is active (bk_alloc.c's bk__alloc_route_sdl), those functions
/// dispatch through the installed base allocator, so an implementation that
/// calls them recurses into itself with no diagnostic.
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

/// Per-system accounting tags. Every framework-internal allocation is counted
/// under exactly one tag, regardless of which allocator serves it. The public
/// bk_alloc()/bk_free() layer is untagged and never touches these counters.
typedef enum BK_MemTag {
  BK_MEM_TAG_APP,   // app core: boot, task system, misc
  BK_MEM_TAG_FRAME, // frame arena backing blocks
  BK_MEM_TAG_GFX,   // gfx object wrappers (buffers, textures, pipelines, ...)
  BK_MEM_TAG_DRAW,  // reserved: allocations made directly by the draw layer
  BK_MEM_TAG_ATLAS, // atlas cache: records, hash, pixel staging, atlas images
  BK_MEM_TAG_SDL,   // SDL-internal allocations, when routed (bk_alloc design spec section 5)
  BK_MEM_TAG_COUNT,
} BK_MemTag;

/// Live/lifetime counters for one tag. Each field is read atomically, but the
/// struct is not a mutually-consistent snapshot -- fine for HUDs and asserts.
typedef struct BK_MemStats {
  isize live_bytes;
  isize live_allocs;
  isize peak_bytes;
  isize total_allocs;
} BK_MemStats;

/// Counters for one tag. Valid any time (all-zero before first use).
BK_MemStats bk_mem_stats(BK_MemTag tag);

/// Short lowercase name for a tag ("gfx", "atlas", ...), for HUDs and logs.
const char *bk_mem_tag_name(BK_MemTag tag);

/// An allocator that BK_ASSERTs (and returns nullptr / does nothing) on any
/// call. Install it where allocation is a bug, turning "does this path
/// allocate?" into a testable assertion.
BK_Allocator bk_allocator_panic(void);

/// State for bk_allocator_counting. Each counter is updated and read
/// atomically (relaxed), so the allocator may serve several threads at once --
/// which it must when installed as BK_AppDesc.allocator, since SDL allocates
/// off the main thread. Reading all four is still not a mutually-consistent
/// snapshot: fine for leak asserts and HUDs, not for arithmetic that assumes
/// the four agree. inner all-zero means the installed base allocator serves
/// the actual memory (the framework default heap when no custom base was
/// installed, and always that default when this allocator is itself the base,
/// since anything else would be self-reference). Zero-initialize before use.
typedef struct BK_CountingAllocator {
  BK_Allocator inner;
  _Atomic isize live_bytes;
  _Atomic isize live_allocs;
  _Atomic isize peak_bytes;
  _Atomic isize total_allocs;
} BK_CountingAllocator;

/// Returns an allocator that forwards to state->inner and maintains the
/// counters. state must outlive every allocation made through it. Mismatched
/// free/realloc sizes surface as nonzero live_bytes at teardown; the leak
/// idiom is: use it for an object's lifetime, destroy, assert live_bytes == 0
/// && live_allocs == 0.
BK_Allocator bk_allocator_counting(BK_CountingAllocator *state);
