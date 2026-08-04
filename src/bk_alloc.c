#include "internal/bk_alloc_internal.h"

#include <bielik/bk_alloc.h>
#include <bielik/bk_app.h> // BK_ASSERT

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

// Deliberately libc malloc/realloc/free, not SDL_malloc/SDL_realloc/SDL_free: this is
// "the default heap" and must stay a fixed primitive that SDL_SetMemoryFunctions cannot
// redirect. bk__alloc_route_sdl (below) makes SDL_malloc itself dispatch through the base
// allocator when routing is active -- if the default heap's own implementation called
// SDL_malloc, resolving to the default heap through any wrapper (bk_allocator_counting
// with an unset inner, the common case) would recurse into the shim forever. See
// DEVIATIONS.md.
static void *s_default_alloc(isize size, void *ctx) {
  (void)ctx;
  return malloc((usize)size);
}

static void *s_default_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  (void)old_size;
  (void)ctx;
  return realloc(ptr, (usize)new_size);
}

static void s_default_free(void *ptr, isize size, void *ctx) {
  (void)size;
  (void)ctx;
  free(ptr);
}

static const BK_Allocator s_default_allocator = {
    .alloc_fn = s_default_alloc, .realloc_fn = s_default_realloc, .free_fn = s_default_free};

static BK_Allocator s_base = {
    .alloc_fn = s_default_alloc, .realloc_fn = s_default_realloc, .free_fn = s_default_free};

bool bk__allocator_valid(const BK_Allocator *a) {
  if (a == nullptr) {
    return true;
  }
  bool any = a->alloc_fn != nullptr || a->realloc_fn != nullptr || a->free_fn != nullptr;
  bool all = a->alloc_fn != nullptr && a->realloc_fn != nullptr && a->free_fn != nullptr;
  return !any || all;
}

/// Resolves the two "use the default" spellings to the installed base.
static const BK_Allocator *s_resolve(const BK_Allocator *a) {
  if (a == nullptr || a->alloc_fn == nullptr) {
    return &s_base;
  }
  return a;
}

static void *s_panic_alloc(isize size, void *ctx) {
  (void)ctx;
  SDL_Log("BK: panic allocator: alloc(%td) on a no-allocation path", size);
  BK_ASSERT(!"panic allocator: alloc");
  return nullptr;
}

static void *s_panic_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  (void)ptr;
  (void)old_size;
  (void)ctx;
  SDL_Log("BK: panic allocator: realloc(%td) on a no-allocation path", new_size);
  BK_ASSERT(!"panic allocator: realloc");
  return nullptr;
}

static void s_panic_free(void *ptr, isize size, void *ctx) {
  (void)ptr;
  (void)size;
  (void)ctx;
  SDL_Log("BK: panic allocator: free on a no-allocation path");
  BK_ASSERT(!"panic allocator: free");
}

BK_Allocator bk_allocator_panic(void) {
  return (BK_Allocator){
      .alloc_fn = s_panic_alloc, .realloc_fn = s_panic_realloc, .free_fn = s_panic_free};
}

// The counters are relaxed atomics because a counting allocator installed as the app-wide
// base serves whatever thread SDL allocates on (the Windows joystick thread, with gamepad
// support initialized). `live` is passed in rather than re-read: reloading live_bytes here
// could see another thread's subtraction and record a peak below the one this call
// actually reached.
static void s_counting_bump_peak(BK_CountingAllocator *c, isize live) {
  isize peak = atomic_load_explicit(&c->peak_bytes, memory_order_relaxed);
  while (live > peak &&
         !atomic_compare_exchange_weak_explicit(&c->peak_bytes, &peak, live, memory_order_relaxed,
                                                memory_order_relaxed)) {
  }
}

static void *s_counting_alloc(isize size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  void *ptr = inner->alloc_fn(size, inner->ctx);
  if (ptr != nullptr) {
    isize live = atomic_fetch_add_explicit(&c->live_bytes, size, memory_order_relaxed) + size;
    atomic_fetch_add_explicit(&c->live_allocs, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->total_allocs, 1, memory_order_relaxed);
    s_counting_bump_peak(c, live);
  }
  return ptr;
}

static void *s_counting_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  void *grown = inner->realloc_fn(ptr, old_size, new_size, inner->ctx);
  if (grown != nullptr) {
    if (ptr == nullptr) { // realloc-from-null is an allocation
      atomic_fetch_add_explicit(&c->live_allocs, 1, memory_order_relaxed);
      atomic_fetch_add_explicit(&c->total_allocs, 1, memory_order_relaxed);
    }
    isize delta = new_size - old_size;
    isize live = atomic_fetch_add_explicit(&c->live_bytes, delta, memory_order_relaxed) + delta;
    s_counting_bump_peak(c, live);
  }
  return grown;
}

static void s_counting_free(void *ptr, isize size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  inner->free_fn(ptr, size, inner->ctx);
  isize live = atomic_fetch_add_explicit(&c->live_bytes, -size, memory_order_relaxed) - size;
  isize allocs = atomic_fetch_add_explicit(&c->live_allocs, -1, memory_order_relaxed) - 1;
  BK_ASSERT(live >= 0 && allocs >= 0);
}

BK_Allocator bk_allocator_counting(BK_CountingAllocator *state) {
  BK_ASSERT(state != nullptr);
  BK_ASSERT(bk__allocator_valid(&state->inner));
  return (BK_Allocator){.alloc_fn = s_counting_alloc,
                        .realloc_fn = s_counting_realloc,
                        .free_fn = s_counting_free,
                        .ctx = state};
}

bool bk__alloc_install(const BK_Allocator *base) {
  if (!bk__allocator_valid(base)) {
    SDL_Log("BK: allocator is partially set: all three functions or none");
    return false;
  }
  if (base == nullptr || base->alloc_fn == nullptr) {
    s_base = s_default_allocator;
  } else {
    s_base = *base;
    // Pin a counting allocator's inner to the default to prevent self-reference when
    // the base is installed as the counting allocator's inner.
    if (base != nullptr && base->ctx != nullptr && base->alloc_fn == s_counting_alloc &&
        ((BK_CountingAllocator *)base->ctx)->inner.alloc_fn == nullptr) {
      ((BK_CountingAllocator *)base->ctx)->inner = s_default_allocator;
    }
  }
  return true;
}

[[noreturn]] static void s_oom(isize size, const char *file, int line) {
  // Nothing here may allocate. With SDL routing active (bk__alloc_route_sdl) every
  // SDL_malloc lands back in the base allocator that just failed, so a diagnostic that
  // allocates -- SDL_Log does on Windows, converting to UTF-16 -- would fail, re-enter
  // s_oom, and recurse until the stack dies instead of aborting. Same for the assert
  // below, which goes through SDL's assertion machinery. So: latch first, and report
  // with fprintf, which never allocates. A second entry (including from another thread,
  // where the first is already on its way to abort) skips straight to abort.
  static _Atomic bool s_reporting;
  if (atomic_exchange_explicit(&s_reporting, true, memory_order_relaxed)) {
    abort();
  }
  // %lld, not %td: clang-cl builds against the UCRT, whose printf predates %td.
  fprintf(stderr, "BK: out of memory (%lld bytes) at %s:%d\n", (long long)size, file, line);
  fflush(stderr); // abort() is not required to flush stdio
  BK_ASSERT(!"out of memory");
  abort(); // deterministic even when the assert is disabled or ignored
}

void *bk__alloc_site(const BK_Allocator *a, isize size, bool zero, const char *file, int line) {
  BK_ASSERT(size >= 0);
  const BK_Allocator *r = s_resolve(a);
  void *ptr;
  if (zero && r->alloc_fn == s_default_alloc) {
    ptr = calloc(1, (usize)size); // let the libc zero fresh pages for free; see
                                  // s_default_alloc's comment on why not SDL_calloc
  } else {
    ptr = r->alloc_fn(size, r->ctx);
    if (ptr != nullptr && zero) {
      SDL_memset(ptr, 0, (usize)size);
    }
  }
  if (ptr == nullptr) {
    s_oom(size, file, line);
  }
  return ptr;
}

void *bk__realloc_site(const BK_Allocator *a, void *ptr, isize old_size, isize new_size,
                       const char *file, int line) {
  BK_ASSERT(old_size >= 0 && new_size >= 0);
  BK_ASSERT(ptr != nullptr || old_size == 0);
  const BK_Allocator *r = s_resolve(a);
  void *grown = r->realloc_fn(ptr, old_size, new_size, r->ctx);
  if (grown == nullptr) {
    s_oom(new_size, file, line);
  }
  return grown;
}

void bk__free_site(const BK_Allocator *a, void *ptr, isize size) {
  if (ptr == nullptr) {
    return;
  }
  BK_ASSERT(size >= 0);
  const BK_Allocator *r = s_resolve(a);
  r->free_fn(ptr, size, r->ctx);
}

typedef struct BK_TagCounters {
  _Atomic isize live_bytes;
  _Atomic isize live_allocs;
  _Atomic isize peak_bytes;
  _Atomic isize total_allocs;
} BK_TagCounters;

static BK_TagCounters s_tags[BK_MEM_TAG_COUNT];

static void s_stats_add(BK_MemTag tag, isize bytes_delta, isize allocs_delta, isize total_delta) {
  BK_ASSERT(tag >= 0 && tag < BK_MEM_TAG_COUNT);
  BK_TagCounters *c = &s_tags[tag];
  isize live =
      atomic_fetch_add_explicit(&c->live_bytes, bytes_delta, memory_order_relaxed) + bytes_delta;
  atomic_fetch_add_explicit(&c->live_allocs, allocs_delta, memory_order_relaxed);
  atomic_fetch_add_explicit(&c->total_allocs, total_delta, memory_order_relaxed);
  isize peak = atomic_load_explicit(&c->peak_bytes, memory_order_relaxed);
  while (live > peak &&
         !atomic_compare_exchange_weak_explicit(&c->peak_bytes, &peak, live, memory_order_relaxed,
                                                memory_order_relaxed)) {
  }
}

BK_MemStats bk_mem_stats(BK_MemTag tag) {
  BK_ASSERT(tag >= 0 && tag < BK_MEM_TAG_COUNT);
  BK_TagCounters *c = &s_tags[tag];
  return (BK_MemStats){
      .live_bytes = atomic_load_explicit(&c->live_bytes, memory_order_relaxed),
      .live_allocs = atomic_load_explicit(&c->live_allocs, memory_order_relaxed),
      .peak_bytes = atomic_load_explicit(&c->peak_bytes, memory_order_relaxed),
      .total_allocs = atomic_load_explicit(&c->total_allocs, memory_order_relaxed),
  };
}

const char *bk_mem_tag_name(BK_MemTag tag) {
  static const char *const names[BK_MEM_TAG_COUNT] = {"app",  "frame", "gfx",
                                                      "draw", "atlas", "sdl"};
  BK_ASSERT(tag >= 0 && tag < BK_MEM_TAG_COUNT);
  return names[tag];
}

void *bk__alloc(const BK_Allocator *a, BK_MemTag tag, isize size, bool zero, const char *file,
                int line) {
  void *ptr = bk__alloc_site(a, size, zero, file, line);
  s_stats_add(tag, size, 1, 1);
  return ptr;
}

void *bk__realloc(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize old_size, isize new_size,
                  const char *file, int line) {
  void *grown = bk__realloc_site(a, ptr, old_size, new_size, file, line);
  s_stats_add(tag, new_size - old_size, ptr == nullptr ? 1 : 0, ptr == nullptr ? 1 : 0);
  return grown;
}

void bk__free(const BK_Allocator *a, BK_MemTag tag, void *ptr, isize size) {
  if (ptr == nullptr) {
    return;
  }
  bk__free_site(a, ptr, size);
  s_stats_add(tag, -size, -1, 0);
}

// ---------------------------------------------------------------------------
// SDL routing (design spec section 5). SDL's memory-function signatures carry
// no ctx and its free carries no size, so each SDL allocation gets a
// max_align_t-sized header storing its total size. Installed only before
// SDL_Init and only when the embedder supplied a custom base allocator.
// ---------------------------------------------------------------------------

typedef union BK_SdlShimHeader {
  isize size; // total size including this header
  max_align_t align;
} BK_SdlShimHeader; // max_align_t: <stddef.h>, already reachable via bk_types.h

static void *s_sdl_shim_malloc(size_t size) {
  isize total = (isize)size + (isize)sizeof(BK_SdlShimHeader);
  BK_SdlShimHeader *header = bk__alloc(nullptr, BK_MEM_TAG_SDL, total, false, __FILE__, __LINE__);
  header->size = total;
  return header + 1;
}

static void *s_sdl_shim_calloc(size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SDL_SIZE_MAX / nmemb) {
    return nullptr; // overflow is a caller error, not OOM -- let SDL handle null
  }
  isize total = (isize)(nmemb * size) + (isize)sizeof(BK_SdlShimHeader);
  BK_SdlShimHeader *header = bk__alloc(nullptr, BK_MEM_TAG_SDL, total, true, __FILE__, __LINE__);
  header->size = total;
  return header + 1;
}

static void *s_sdl_shim_realloc(void *mem, size_t size) {
  if (mem == nullptr) {
    return s_sdl_shim_malloc(size);
  }
  BK_SdlShimHeader *header = (BK_SdlShimHeader *)mem - 1;
  isize old_total = header->size;
  isize new_total = (isize)size + (isize)sizeof(BK_SdlShimHeader);
  BK_SdlShimHeader *grown =
      bk__realloc(nullptr, BK_MEM_TAG_SDL, header, old_total, new_total, __FILE__, __LINE__);
  grown->size = new_total;
  return grown + 1;
}

static void s_sdl_shim_free(void *mem) {
  if (mem == nullptr) {
    return;
  }
  BK_SdlShimHeader *header = (BK_SdlShimHeader *)mem - 1;
  bk__free(nullptr, BK_MEM_TAG_SDL, header, header->size);
}

bool bk__alloc_route_sdl(void) {
  if (s_base.alloc_fn == s_default_alloc) {
    return false; // default heap: routing would be a no-op with overhead
  }
  // Every pointer SDL frees through the shim must have been allocated through it: the
  // shim reads a size header written in front of the allocation, and an allocation that
  // predates the shim has no such header. So refuse to route if SDL has allocated
  // already -- an embedder calling SDL_SetHint or SDL_Log before bk_run is enough. The
  // cost of refusing is an undercount on BK_MEM_TAG_SDL; the cost of routing anyway is
  // heap corruption on the first free of a pre-shim pointer.
  //
  // A negative count means this SDL was built without SDL_TRACK_ALLOCATION_COUNT, so
  // there is nothing to check and the pre-guard behavior stands: route and assume clean.
  // The top-level CMakeLists defines it for the SDL we build; a consumer linking their
  // own SDL may not.
  int live = SDL_GetNumAllocations();
  if (live > 0) {
    SDL_Log("BK: %d SDL allocation(s) predate boot; SDL not routed through the app allocator",
            live);
    return false;
  }
  // Nothing above this line may call into SDL on the routing path -- SDL_Log itself
  // allocates (hint properties, log state), which would trip the very check above on the
  // next boot and, worse, hand the shim a pointer it did not allocate.
  if (!SDL_SetMemoryFunctions(s_sdl_shim_malloc, s_sdl_shim_calloc, s_sdl_shim_realloc,
                              s_sdl_shim_free)) {
    SDL_Log("BK: SDL_SetMemoryFunctions failed: %s", SDL_GetError());
    return false;
  }
  return true;
}
