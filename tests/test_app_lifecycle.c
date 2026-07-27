#include "bk_test.h"
#include <bielik/bk_app.h>
#include <stdio.h>

static int s_update_calls = 0;

static BK_Result test_init(void **state, int argc, char **argv) {
    (void)state;
    (void)argc;
    (void)argv;
    REQUIRE(bk_window() != NULL);
    REQUIRE(bk_gpu() != NULL);
    return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *f) {
    (void)state;
    s_update_calls++;
    REQUIRE_EQ_U64(f->tick, (uint64_t)s_update_calls);
    REQUIRE_NEAR(f->dt, 1.0 / 60.0, 1e-9);
    REQUIRE(f->alpha == 0.0);
    REQUIRE(f->real_time >= 0.0);
    if (s_update_calls >= 3) {
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
        .window = {.title = "test_app_lifecycle", .w = 64, .h = 64},
        .time = {.tick_hz = 60},
        .init = test_init,
        .update = test_update,
        .render = test_render,
    };
    int result = bk_run(&desc, argc, argv);
    REQUIRE(result == 0);
    REQUIRE(s_update_calls == 3);
    printf("test_app_lifecycle: OK\n");
    return 0;
}
