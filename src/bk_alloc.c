#include "internal/bk_alloc_internal.h"

#include <bielik/bk_alloc.h>
#include <bielik/bk_app.h> // BK_ASSERT

#include <SDL3/SDL.h>

#include <stdlib.h>

static void *s_default_alloc(isize size, void *ctx) {
  (void)ctx;
  return SDL_malloc((usize)size);
}

static void *s_default_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  (void)old_size;
  (void)ctx;
  return SDL_realloc(ptr, (usize)new_size);
}

static void s_default_free(void *ptr, isize size, void *ctx) {
  (void)size;
  (void)ctx;
  SDL_free(ptr);
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

static void s_counting_bump_peak(BK_CountingAllocator *c) {
  if (c->live_bytes > c->peak_bytes) {
    c->peak_bytes = c->live_bytes;
  }
}

static void *s_counting_alloc(isize size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  void *ptr = inner->alloc_fn(size, inner->ctx);
  if (ptr != nullptr) {
    c->live_bytes += size;
    c->live_allocs++;
    c->total_allocs++;
    s_counting_bump_peak(c);
  }
  return ptr;
}

static void *s_counting_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  void *grown = inner->realloc_fn(ptr, old_size, new_size, inner->ctx);
  if (grown != nullptr) {
    if (ptr == nullptr) { // realloc-from-null is an allocation
      c->live_allocs++;
      c->total_allocs++;
    }
    c->live_bytes += new_size - old_size;
    s_counting_bump_peak(c);
  }
  return grown;
}

static void s_counting_free(void *ptr, isize size, void *ctx) {
  BK_CountingAllocator *c = ctx;
  const BK_Allocator *inner = s_resolve(&c->inner);
  inner->free_fn(ptr, size, inner->ctx);
  c->live_bytes -= size;
  c->live_allocs--;
  BK_ASSERT(c->live_bytes >= 0 && c->live_allocs >= 0);
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
  SDL_Log("BK: out of memory (%td bytes) at %s:%d", size, file, line);
  BK_ASSERT(!"out of memory");
  abort(); // deterministic even when the assert is disabled or ignored
}

void *bk__alloc_site(const BK_Allocator *a, isize size, bool zero, const char *file, int line) {
  BK_ASSERT(size >= 0);
  const BK_Allocator *r = s_resolve(a);
  void *ptr;
  if (zero && r->alloc_fn == s_default_alloc) {
    ptr = SDL_calloc(1, (usize)size); // let the libc zero fresh pages for free
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
