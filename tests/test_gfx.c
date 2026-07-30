#include "bk_test.h"
#include "internal/bk_gfx_internal.h"
#include <bielik/bk_gfx.h>

static void test_default_clear_color(void) {
    BK_Color c = bk__gfx_get_clear_color();
    REQUIRE_NEAR(c.r, 0.1f, 1e-6);
    REQUIRE_NEAR(c.g, 0.1f, 1e-6);
    REQUIRE_NEAR(c.b, 0.12f, 1e-6);
    REQUIRE_NEAR(c.a, 1.0f, 1e-6);
}

static void test_set_then_get_round_trips(void) {
    bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});

    BK_Color c = bk__gfx_get_clear_color();
    REQUIRE_NEAR(c.r, 0.25f, 1e-6);
    REQUIRE_NEAR(c.g, 0.5f, 1e-6);
    REQUIRE_NEAR(c.b, 0.75f, 1e-6);
    REQUIRE_NEAR(c.a, 1.0f, 1e-6);
}

static void test_last_set_wins(void) {
    bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});
    bk_gfx_set_clear_color((BK_Color){.r = 0.9f, .g = 0.8f, .b = 0.7f, .a = 0.6f});

    BK_Color c = bk__gfx_get_clear_color();
    REQUIRE_NEAR(c.r, 0.9f, 1e-6);
    REQUIRE_NEAR(c.g, 0.8f, 1e-6);
    REQUIRE_NEAR(c.b, 0.7f, 1e-6);
    REQUIRE_NEAR(c.a, 0.6f, 1e-6);
}

static void test_bind_pipeline_and_draw_sets_pending_state(void) {
    static int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);

    REQUIRE(bk__gfx_get_pending_pipeline() == fake_pipeline);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 3);
}

static void test_flush_early_return_clears_pending_state(void) {
    int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);

    // No app has been booted in this test binary, so bk_gpu() returns nullptr and
    // SDL_AcquireGPUCommandBuffer fails immediately -- this exercises bk__gfx_flush's
    // early-return path (command-buffer acquire failure) without needing a real
    // window/GPU device, and proves pending state doesn't survive it.
    bk__gfx_flush();

    REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 0);
}

int main(void) {
    test_default_clear_color();
    test_set_then_get_round_trips();
    test_last_set_wins();
    test_bind_pipeline_and_draw_sets_pending_state();
    test_flush_early_return_clears_pending_state();
    printf("test_gfx: OK\n");
    return 0;
}
