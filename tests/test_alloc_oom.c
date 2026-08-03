// Drives the wrapper's OOM path: a failing allocator makes bk_alloc log
// "BK: out of memory" and abort. Registered with PASS_REGULAR_EXPRESSION on
// that log line -- CTest then ignores the (deliberate) crash exit.
#include <bielik/bk_alloc.h>

#include <stdio.h>

static void *s_null_alloc(isize size, void *ctx) {
  (void)size;
  (void)ctx;
  return nullptr;
}

static void *s_null_realloc(void *ptr, isize old_size, isize new_size, void *ctx) {
  (void)ptr;
  (void)old_size;
  (void)new_size;
  (void)ctx;
  return nullptr;
}

static void s_null_free(void *ptr, isize size, void *ctx) {
  (void)ptr;
  (void)size;
  (void)ctx;
}

int main(void) {
  BK_Allocator failing = {
      .alloc_fn = s_null_alloc, .realloc_fn = s_null_realloc, .free_fn = s_null_free};
  void *p = bk_alloc(&failing, 64); // must log and abort; never returns
  printf("unreachable: bk_alloc returned %p\n", p);
  return 0;
}
