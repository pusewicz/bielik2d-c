#include "bk_test.h"

#include <bielik/bk_app.h>

#include <stdio.h>

static bool s_quit_called = false;

static BK_Result test_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;
  // Deliberately return BK_FAIL without touching *state, matching the shape
  // every sample uses (state is only assigned on init's success path) --
  // this is the exact failure mode from bielik2d-c#7.
  return BK_FAIL;
}

static void test_quit(void *state, BK_Result result) {
  (void)state;
  (void)result;
  s_quit_called = true;
}

int main(int argc, char **argv) {
  // This test's whole point is to make boot fail, and a Release build answers that with
  // a modal error dialog -- which nothing here can dismiss, so the test would hang until
  // ctest's timeout killed it (bielik2d-c#22). Opt out of the dialog.
  SDL_SetHint(BK_HINT_NO_ERROR_DIALOG, "1");

  BK_AppDesc desc = {
      .window = {.title = "test_app_init_failure", .width = 64, .height = 64},
      .init = test_init,
      .quit = test_quit,
  };
  int result = bk_run(&desc, argc, argv);
  REQUIRE(result != 0);
  REQUIRE(!s_quit_called);
  printf("test_app_init_failure: OK\n");
  return 0;
}
