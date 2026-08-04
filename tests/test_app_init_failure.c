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

typedef struct LogCapture {
  bool saw_partially_set;
} LogCapture;

static void SDLCALL s_capture_partial_allocator_log(void *userdata, int category,
                                                    SDL_LogPriority priority, const char *message) {
  (void)category;
  (void)priority;
  LogCapture *capture = userdata;
  if (SDL_strstr(message, "partially set") != nullptr) {
    capture->saw_partially_set = true;
  }
}

// Trivial stand-in for one leg of BK_Allocator; never actually called since boot must
// reject the partial struct before any allocation happens.
static void *s_partial_alloc_fn(isize size, void *ctx) {
  (void)size;
  (void)ctx;
  return nullptr;
}

// Boot rejects the partial allocator before SDL_Init, so no window/GPU device ever gets
// created here.
static void test_partial_allocator_fails_boot(void) {
  SDL_LogOutputFunction prev_fn;
  void *prev_userdata;
  SDL_GetLogOutputFunction(&prev_fn, &prev_userdata);
  LogCapture capture = {0};
  SDL_SetLogOutputFunction(s_capture_partial_allocator_log, &capture);

  BK_AppDesc desc = {
      .window = {.title = "test_app_init_failure_partial_allocator", .width = 64, .height = 64},
      .allocator = {.alloc_fn = s_partial_alloc_fn}, // partial: only one of three functions set
  };
  int result = bk_run(&desc, 0, nullptr);

  SDL_SetLogOutputFunction(prev_fn, prev_userdata);

  REQUIRE(result != 0);
  REQUIRE(capture.saw_partially_set);
}

static void test_init_callback_failure(int argc, char **argv) {
  BK_AppDesc desc = {
      .window = {.title = "test_app_init_failure", .width = 64, .height = 64},
      .init = test_init,
      .quit = test_quit,
  };
  int result = bk_run(&desc, argc, argv);
  REQUIRE(result != 0);
  REQUIRE(!s_quit_called);
}

int main(int argc, char **argv) {
  // This test's whole point is to make boot fail, and a Release build answers that with
  // a modal error dialog -- which nothing here can dismiss, so the test would hang until
  // ctest's timeout killed it (bielik2d-c#22). Opt out of the dialog.
  SDL_SetHint(BK_HINT_NO_ERROR_DIALOG, "1");

  test_init_callback_failure(argc, argv);
  test_partial_allocator_fails_boot();

  printf("test_app_init_failure: OK\n");
  return 0;
}
