#include "bk_test.h"

#include <bielik/bk_alloc.h>
#include <bielik/bk_app.h>

#include <stdio.h>

static int s_update_calls = 0;
static BK_CountingAllocator s_counter; // zero-initialized static; inner all-zero => default heap

static BK_Result test_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;
  REQUIRE(bk_window() != nullptr);
  REQUIRE(bk_gpu() != nullptr);
  return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *frame) {
  (void)state;
  s_update_calls++;
  REQUIRE_EQ_U64(frame->tick, (u64)s_update_calls);
  REQUIRE_NEAR(frame->dt, 1.0 / 60.0, 1e-9);
  REQUIRE(frame->alpha == 0.0);
  REQUIRE(frame->real_time >= 0.0);
  if (s_update_calls >= 3) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  // A no-op render never touches the frame arena (nothing in the framework calls
  // bk_frame_alloc unless there is draw geometry to pack), so the allocator assertions
  // below need a direct call to exercise BK_MEM_TAG_FRAME's arena-chunk allocation.
  bk_frame_alloc(16, 0);
}

int main(int argc, char **argv) {
  BK_Allocator counted = bk_allocator_counting(&s_counter);
  BK_AppDesc desc = {
      .window = {.title = "test_app_lifecycle", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .init = test_init,
      .update = test_update,
      .render = test_render,
      .allocator = counted,
  };
  int result = bk_run(&desc, argc, argv);
  REQUIRE(result == 0);
  REQUIRE(s_update_calls == 3);
  REQUIRE(s_counter.total_allocs > 0);           // the framework routed through it
  REQUIRE_EQ_U64((u64)s_counter.live_allocs, 0); // and freed everything it allocated
  REQUIRE_EQ_U64((u64)s_counter.live_bytes, 0);
  REQUIRE(bk_mem_stats(BK_MEM_TAG_FRAME).total_allocs > 0); // arena chunks were tagged
  printf("test_app_lifecycle: OK\n");
  return 0;
}
