#include "bk_test.h"
#include "internal/bk_gfx_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>

#include <stdio.h>

static int s_update_calls = 0;
static bool s_resize_requested = false;
static bool s_invariant_checked = false;

static BK_Result test_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;
  REQUIRE(bk_window() != nullptr);
  REQUIRE(bk_gpu() != nullptr);

  // Deterministic before anything external (a WM, a display server) has a chance
  // to interfere: right after boot, bk_window_size must equal what was requested.
  i32 width = 0, height = 0;
  bk_window_size(&width, &height);
  REQUIRE(width == 64);
  REQUIRE(height == 64);
  return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *f) {
  (void)state;
  (void)f;
  s_update_calls++;

  // Request the resize once a couple of frames in (so the first flush has already
  // created the framework depth texture at the original size -- this exercises the
  // recreate path, not just first-creation).
  if (s_update_calls == 3 && !s_resize_requested) {
    SDL_SetWindowSize(bk_window(), 96, 96);
    s_resize_requested = true;
  }

  // Checked from inside update (bk_window()/bk_gpu() are only valid between init
  // and quit -- by the time bk_run returns, the window and GPU device are already
  // destroyed). Several frames after the resize request, so any resize event has
  // been pumped and at least one more flush has run.
  //
  // Not asserting the window actually became 96x96: a window manager under a
  // headless/CI display server may not honor the request. What must hold either
  // way is the invariant below: bk_window_size agrees with SDL's own drawable-size
  // query, and the framework-owned swapchain depth texture (created because
  // .depth_stencil was requested) is sized to exactly that -- not to whatever it
  // was at first creation, and not to the 96x96 requested size if the WM didn't
  // grant it.
  if (s_update_calls == 8 && !s_invariant_checked) {
    s_invariant_checked = true;

    int reported_w = 0, reported_h = 0;
    bk_window_size(&reported_w, &reported_h);

    int actual_w = 0, actual_h = 0;
    SDL_GetWindowSizeInPixels(bk_window(), &actual_w, &actual_h);
    REQUIRE(reported_w == actual_w);
    REQUIRE(reported_h == actual_h);

    int depth_w = 0, depth_h = 0;
    bk__gfx_get_swapchain_depth_size(&depth_w, &depth_h);
    REQUIRE(depth_w == reported_w);
    REQUIRE(depth_h == reported_h);
  }

  if (s_update_calls >= 10) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *f) {
  (void)state;
  (void)f;
}

int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .window = {.title = "test_gfx_resize",
                 .width = 64,
                 .height = 64,
                 .resizable = true,
                 .depth_stencil = true},
      .time = {.tick_hz = 60},
      .init = test_init,
      .update = test_update,
      .render = test_render,
  };
  int result = bk_run(&desc, argc, argv);
  REQUIRE(result == 0);
  REQUIRE(s_resize_requested);
  REQUIRE(s_invariant_checked);

  printf("test_gfx_resize: OK\n");
  return 0;
}
