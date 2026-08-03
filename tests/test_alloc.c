#include "bk_test.h"
#include "internal/bk_alloc_internal.h"

#include <bielik/bk_alloc.h>

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
  // a == nullptr and all-zero struct both mean the default SDL heap.
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

int main(void) {
  test_default_roundtrip();
  test_alloc_zero_zeroes();
  test_realloc_preserves();
  test_new_macros();
  test_custom_allocator_forwarding();
  test_install_validation();
  printf("test_alloc: OK\n");
  return 0;
}
