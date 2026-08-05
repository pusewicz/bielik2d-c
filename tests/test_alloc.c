#include "bk_test.h"
#include "internal/bk_alloc_internal.h"

#include <bielik/bk_alloc.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

// A minimal logging allocator: records the last call so tests can assert the
// wrappers forward sizes and ctx faithfully. Backed by the default heap.
typedef struct LogAlloc {
  int alloc_calls, realloc_calls, free_calls;
  isize last_size, last_old_size, last_new_size, last_free_size;
} LogAlloc;

#include <stdlib.h>

static void *s_log_alloc(isize size, void *ctx) {
  LogAlloc *la = ctx;
  la->alloc_calls++;
  la->last_size = size;
  return malloc((size_t)size);
}

static void *s_log_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  LogAlloc *la = ctx;
  la->realloc_calls++;
  la->last_old_size = old_size;
  la->last_new_size = new_size;
  return realloc(ptr, (size_t)new_size);
}

static void s_log_free(void *ptr, isize size, void *ctx) {
  LogAlloc *la = ctx;
  la->free_calls++;
  la->last_free_size = size;
  free(ptr);
}

static void test_default_roundtrip(void) {
  // a == nullptr and all-zero struct both mean the default heap (libc malloc/free).
  void *p = bk_alloc(nullptr, 64);
  REQUIRE(p != nullptr);
  bk_free(nullptr, p, 64);

  BK_Allocator zero = {0};
  void *q = bk_alloc(&zero, 32);
  REQUIRE(q != nullptr);
  bk_free(&zero, q, 32);
}

static void test_alloc_zero_zeroes(void) {
  unsigned char *p = bk_alloc_zero(nullptr, 256);
  REQUIRE(p != nullptr);
  bool all_zero = true;
  for (int i = 0; i < 256; i++) {
    all_zero = all_zero && (p[i] == 0);
  }
  REQUIRE(all_zero);
  bk_free(nullptr, p, 256);
}

static void test_realloc_preserves(void) {
  char *p = bk_alloc(nullptr, 8);
  REQUIRE(p != nullptr);
  memcpy(p, "bielik!", 8);
  p = bk_realloc(nullptr, p, 8, 4096);
  REQUIRE(p != nullptr);
  REQUIRE(memcmp(p, "bielik!", 8) == 0);
  bk_free(nullptr, p, 4096);
}

static void test_new_macros(void) {
  typedef struct {
    i32 x, y;
  } Pair;

  Pair *one = BK_NEW(nullptr, Pair);
  REQUIRE(one != nullptr);
  REQUIRE(one->x == 0 && one->y == 0);
  bk_free(nullptr, one, (isize)sizeof(Pair));

  Pair *many = BK_NEW_ARRAY(nullptr, Pair, 16);
  REQUIRE(many != nullptr);
  REQUIRE(many[15].x == 0 && many[15].y == 0);
  bk_free(nullptr, many, (isize)sizeof(Pair) * 16);
}

static void test_custom_allocator_forwarding(void) {
  LogAlloc la = {0};
  BK_Allocator a = {
      .alloc_fn = s_log_alloc, .realloc_fn = s_log_realloc, .free_fn = s_log_free, .ctx = &la};

  void *p = bk_alloc(&a, 100);
  REQUIRE(la.alloc_calls == 1);
  REQUIRE_EQ_U64((u64)la.last_size, 100);

  p = bk_realloc(&a, p, 100, 200);
  REQUIRE(la.realloc_calls == 1);
  REQUIRE_EQ_U64((u64)la.last_old_size, 100);
  REQUIRE_EQ_U64((u64)la.last_new_size, 200);

  // bk_alloc_zero through a custom allocator memsets in the wrapper.
  unsigned char *z = bk_alloc_zero(&a, 64);
  REQUIRE(la.alloc_calls == 2);
  REQUIRE(z[0] == 0 && z[63] == 0);
  bk_free(&a, z, 64);
  REQUIRE(la.free_calls == 1);
  REQUIRE_EQ_U64((u64)la.last_free_size, 64);

  bk_free(&a, p, 200);
  REQUIRE(la.free_calls == 2);

  // Freeing nullptr is a no-op and must not reach the allocator.
  bk_free(&a, nullptr, 0);
  REQUIRE(la.free_calls == 2);
}

static void test_install_validation(void) {
  // All-zero and fully-set are valid; any partial combination is not.
  BK_Allocator zero = {0};
  REQUIRE(bk__allocator_valid(&zero));
  REQUIRE(bk__allocator_valid(nullptr));

  LogAlloc la = {0};
  BK_Allocator full = {
      .alloc_fn = s_log_alloc, .realloc_fn = s_log_realloc, .free_fn = s_log_free, .ctx = &la};
  REQUIRE(bk__allocator_valid(&full));

  BK_Allocator partial = {.alloc_fn = s_log_alloc};
  REQUIRE(!bk__allocator_valid(&partial));
  REQUIRE(!bk__alloc_install(&partial));

  // A valid install routes wrapper traffic with a == nullptr to the base.
  REQUIRE(bk__alloc_install(&full));
  void *p = bk_alloc(nullptr, 48);
  REQUIRE(la.alloc_calls == 1);
  bk_free(nullptr, p, 48);
  REQUIRE(la.free_calls == 1);

  // nullptr resets to the default heap.
  REQUIRE(bk__alloc_install(nullptr));
  void *q = bk_alloc(nullptr, 48);
  REQUIRE(la.alloc_calls == 1); // unchanged
  bk_free(nullptr, q, 48);
}

static void test_counting_allocator(void) {
  BK_CountingAllocator counter = {0}; // inner all-zero => the installed base, reset to the
                                      // default heap by the test above
  BK_Allocator a = bk_allocator_counting(&counter);

  void *p = bk_alloc(&a, 100);
  void *q = bk_alloc(&a, 50);
  REQUIRE_EQ_U64((u64)counter.live_bytes, 150);
  REQUIRE_EQ_U64((u64)counter.live_allocs, 2);
  REQUIRE_EQ_U64((u64)counter.peak_bytes, 150);
  REQUIRE_EQ_U64((u64)counter.total_allocs, 2);

  p = bk_realloc(&a, p, 100, 300);
  REQUIRE_EQ_U64((u64)counter.live_bytes, 350);
  REQUIRE_EQ_U64((u64)counter.peak_bytes, 350);
  REQUIRE_EQ_U64((u64)counter.total_allocs, 2); // realloc of a live ptr is not a new allocation

  // realloc from nullptr IS an allocation (the atlas growth paths start their
  // arrays this way) and must count as one, or its eventual free goes negative.
  void *r = bk_realloc(&a, nullptr, 0, 40);
  REQUIRE_EQ_U64((u64)counter.live_allocs, 3);
  REQUIRE_EQ_U64((u64)counter.total_allocs, 3);
  bk_free(&a, r, 40);

  bk_free(&a, q, 50);
  REQUIRE_EQ_U64((u64)counter.live_bytes, 300);
  REQUIRE_EQ_U64((u64)counter.live_allocs, 1);
  REQUIRE_EQ_U64((u64)counter.peak_bytes, 390); // peak never decreases

  bk_free(&a, p, 300);
  REQUIRE_EQ_U64((u64)counter.live_bytes, 0);
  REQUIRE_EQ_U64((u64)counter.live_allocs, 0);
}

// The panic allocator is exercised by calling its functions directly, NOT
// through bk_alloc: through the wrapper its nullptr return would hit the OOM
// abort. Swap SDL's assertion handler so the BK_ASSERT inside is observed
// instead of aborting (tests run with SDL_ASSERT=abort).
static int s_panic_hits = 0;

static SDL_AssertState SDLCALL s_ignore_assert(const SDL_AssertData *data, void *userdata) {
  (void)data;
  (void)userdata;
  s_panic_hits++;
  return SDL_ASSERTION_IGNORE;
}

static void test_panic_allocator(void) {
  BK_Allocator p = bk_allocator_panic();
  void *prev_data = nullptr;
  SDL_AssertionHandler prev = SDL_GetAssertionHandler(&prev_data);
  SDL_SetAssertionHandler(s_ignore_assert, nullptr);

  REQUIRE(p.alloc_fn(16, p.ctx) == nullptr);
  REQUIRE(s_panic_hits == 1);
  REQUIRE(p.realloc_fn(nullptr, 0, 16, p.ctx) == nullptr);
  REQUIRE(s_panic_hits == 2);
  p.free_fn(&s_panic_hits, 4, p.ctx); // any pointer; must not be dereferenced
  REQUIRE(s_panic_hits == 3);

  SDL_SetAssertionHandler(prev, prev_data);
}

static void test_mem_stats(void) {
  BK_MemStats before = bk_mem_stats(BK_MEM_TAG_GFX);

  void *p = BK__ALLOC(nullptr, BK_MEM_TAG_GFX, 1000);
  BK_MemStats during = bk_mem_stats(BK_MEM_TAG_GFX);
  REQUIRE_EQ_U64((u64)(during.live_bytes - before.live_bytes), 1000);
  REQUIRE_EQ_U64((u64)(during.live_allocs - before.live_allocs), 1);
  REQUIRE_EQ_U64((u64)(during.total_allocs - before.total_allocs), 1);
  REQUIRE(during.peak_bytes >= before.live_bytes + 1000);

  p = BK__REALLOC(nullptr, BK_MEM_TAG_GFX, p, 1000, 4000);
  BK_MemStats grown = bk_mem_stats(BK_MEM_TAG_GFX);
  REQUIRE_EQ_U64((u64)(grown.live_bytes - before.live_bytes), 4000);
  REQUIRE_EQ_U64((u64)(grown.total_allocs - before.total_allocs), 1); // realloc: not a new alloc

  BK__FREE(nullptr, BK_MEM_TAG_GFX, p, 4000);
  BK_MemStats after = bk_mem_stats(BK_MEM_TAG_GFX);
  REQUIRE_EQ_U64((u64)(after.live_bytes - before.live_bytes), 0);
  REQUIRE_EQ_U64((u64)(after.live_allocs - before.live_allocs), 0);

  // Public-layer calls do NOT touch tag counters.
  void *q = bk_alloc(nullptr, 512);
  BK_MemStats untouched = bk_mem_stats(BK_MEM_TAG_GFX);
  REQUIRE_EQ_U64((u64)untouched.live_bytes, (u64)after.live_bytes);
  bk_free(nullptr, q, 512);
}

static void test_mem_tag_names(void) {
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_APP), "app") == 0);
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_FRAME), "frame") == 0);
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_GFX), "gfx") == 0);
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_DRAW), "draw") == 0);
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_ATLAS), "atlas") == 0);
  REQUIRE(strcmp(bk_mem_tag_name(BK_MEM_TAG_SDL), "sdl") == 0);
}

// SDL routing hands SDL a shim that reads a size header written in front of every
// allocation the shim itself made. An SDL allocation made before the shim was installed
// has no such header, so routing a process where SDL has already allocated corrupts the
// heap the first time SDL frees one of those pointers. bk__alloc_route_sdl refuses.
//
// The dirty allocation is made here rather than relied on: by this point SDLTest's logging
// has certainly allocated through SDL, but "certainly" is a per-platform guess and this test
// runs in CI's required job everywhere. One SDL_strdup makes the premise true by
// construction -- it is exactly what an embedder's SDL_SetHint does before bk_run. The
// premise is still asserted, so a build that loses SDL_TRACK_ALLOCATION_COUNT (count reads
// -1, routing proceeds) fails here instead of passing vacuously.
static void test_sdl_routing_refuses_after_sdl_has_allocated(void) {
  char *dirty = SDL_strdup("x"); // an SDL allocation the shim did not make
  REQUIRE(dirty != nullptr);
  REQUIRE(SDL_GetNumAllocations() > 0);

  BK_CountingAllocator counter = {0};
  BK_Allocator base = bk_allocator_counting(&counter);
  REQUIRE(bk__alloc_install(&base));
  REQUIRE(!bk__alloc_route_sdl()); // routed anyway => SDL frees a header that isn't there
  REQUIRE(bk__alloc_install(nullptr));

  REQUIRE_EQ_U64((u64)bk_mem_stats(BK_MEM_TAG_SDL).total_allocs, 0);
  SDL_free(dirty); // safe precisely because routing was refused
}

int main(void) {
  test_default_roundtrip();
  test_alloc_zero_zeroes();
  test_realloc_preserves();
  test_new_macros();
  test_custom_allocator_forwarding();
  test_install_validation();
  test_counting_allocator();
  test_panic_allocator();
  test_mem_stats();
  test_mem_tag_names();
  test_sdl_routing_refuses_after_sdl_has_allocated();
  printf("test_alloc: OK\n");
  return 0;
}
