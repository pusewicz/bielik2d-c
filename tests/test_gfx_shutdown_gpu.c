#include "bk_test.h"
#include "internal/bk_gfx_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_canvas.h>

#include <stdio.h>

// Regression test for issue #39: bk__gfx_shutdown destroyed the swapchain depth
// texture but left the pending draw chain and bound canvas pointing into memory a
// second bk_run in the same process would find already freed. bk__iterate's tail
// (bk__draw_collate, bk__gfx_flush, bk__arena_reset) runs unconditionally on every
// call -- including the very first, before an app's own update necessarily runs a
// single time, since the first tick after boot commonly has cf.ticks == 0 -- so a
// prior run's dangling state gets walked before the new run's app code can replace
// it. Each first-run callback below records one such piece of stale state and
// returns BK_DONE on the same tick, which skips bk__iterate's tail entirely (see
// bk__gfx_shutdown's comment) and leaves it unlinked when the app's memory is freed.

static BK_Result s_record_draw_and_quit(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_gfx_draw(3);
  return BK_DONE;
}

static BK_Result s_bind_a_destroyed_canvas_and_quit(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  BK_GfxCanvasDesc desc = {.width = 8, .height = 8};
  BK_GfxCanvas *canvas = bk_gfx_canvas_create(bk_gpu(), &desc);
  REQUIRE(canvas != nullptr);
  bk_gfx_bind_canvas(canvas);
  bk_gfx_canvas_destroy(canvas); // dangling in s_pending_canvas from here on
  return BK_DONE;
}

static BK_Result s_quit_immediately(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  return BK_DONE;
}

static void s_run_first_then_second(BK_Result (*first_update)(void *, const BK_FrameInfo *)) {
  BK_AppDesc first = {
      .window = {.title = "test_gfx_shutdown_first", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .update = first_update,
  };
  REQUIRE(bk_run(&first, 0, nullptr) == 0);

  BK_AppDesc second = {
      .window = {.title = "test_gfx_shutdown_second", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .update = s_quit_immediately,
  };
  REQUIRE(bk_run(&second, 0, nullptr) == 0);
  REQUIRE(bk__gfx_get_draw_count() == 0);
}

static void test_second_run_does_not_see_a_stale_draw_chain(void) {
  s_run_first_then_second(s_record_draw_and_quit);
}

static void test_second_run_does_not_see_a_stale_canvas(void) {
  s_run_first_then_second(s_bind_a_destroyed_canvas_and_quit);
}

int main(void) {
  test_second_run_does_not_see_a_stale_draw_chain();
  test_second_run_does_not_see_a_stale_canvas();
  printf("test_gfx_shutdown_gpu: OK\n");
  return 0;
}
