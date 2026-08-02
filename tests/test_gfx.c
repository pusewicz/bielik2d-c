#include "bk_test.h"
#include "internal/bk_gfx_internal.h"

#include <bielik/bk_gfx.h>

static void test_default_clear_color(void) {
  BK_Color color = bk__gfx_get_clear_color();
  REQUIRE_NEAR(color.r, 0.1f, 1e-6);
  REQUIRE_NEAR(color.g, 0.1f, 1e-6);
  REQUIRE_NEAR(color.b, 0.12f, 1e-6);
  REQUIRE_NEAR(color.a, 1.0f, 1e-6);
}

static void test_set_then_get_round_trips(void) {
  bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});

  BK_Color color = bk__gfx_get_clear_color();
  REQUIRE_NEAR(color.r, 0.25f, 1e-6);
  REQUIRE_NEAR(color.g, 0.5f, 1e-6);
  REQUIRE_NEAR(color.b, 0.75f, 1e-6);
  REQUIRE_NEAR(color.a, 1.0f, 1e-6);
}

static void test_last_set_wins(void) {
  bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});
  bk_gfx_set_clear_color((BK_Color){.r = 0.9f, .g = 0.8f, .b = 0.7f, .a = 0.6f});

  BK_Color color = bk__gfx_get_clear_color();
  REQUIRE_NEAR(color.r, 0.9f, 1e-6);
  REQUIRE_NEAR(color.g, 0.8f, 1e-6);
  REQUIRE_NEAR(color.b, 0.7f, 1e-6);
  REQUIRE_NEAR(color.a, 0.6f, 1e-6);
}

static void test_request_capture_sets_pending_path(void) {
  bk_gfx_request_capture("screenshot.bmp");

  REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "screenshot.bmp") == 0);
}

static void test_bind_pipeline_and_draw_sets_pending_state(void) {
  static int dummy;
  BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;
  i32 draws_before = bk__gfx_get_draw_count();

  bk_gfx_bind_pipeline(fake_pipeline);
  bk_gfx_draw(3);

  REQUIRE(bk__gfx_get_pending_pipeline() == fake_pipeline);
  // The bind is sticky state; the draw appended a record capturing it. Asserted
  // relative to the count on entry, since nothing flushes between test functions.
  REQUIRE(bk__gfx_get_draw_count() == draws_before + 1);
  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(bk__gfx_get_draw_count() - 1);
  REQUIRE(draw != nullptr);
  REQUIRE(draw->vertex_count == 3);
  REQUIRE(draw->pipeline == fake_pipeline);
  REQUIRE(draw->instance_count == 1);
}

static void test_flush_early_return_clears_pending_state(void) {
  int dummy;
  BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;
  BK_GfxBuffer *fake_vertex_buffer = (BK_GfxBuffer *)&dummy;
  BK_GfxBuffer *fake_index_buffer = (BK_GfxBuffer *)&dummy;
  BK_GfxTexture *fake_texture = (BK_GfxTexture *)&dummy;
  BK_GfxSampler *fake_sampler = (BK_GfxSampler *)&dummy;
  BK_GfxCanvas *fake_canvas = (BK_GfxCanvas *)&dummy;

  // Start from a known-clear state: earlier test functions record draws and nothing
  // flushes between them.
  bk__gfx_flush();
  REQUIRE(bk__gfx_get_draw_count() == 0);

  bk_gfx_bind_pipeline(fake_pipeline);
  bk_gfx_draw(3);
  bk_gfx_bind_vertex_buffer(fake_vertex_buffer);
  bk_gfx_bind_index_buffer(fake_index_buffer);
  bk_gfx_bind_texture(fake_texture, fake_sampler);
  bk_gfx_draw_indexed(6);
  bk_gfx_bind_canvas(fake_canvas);
  bk_gfx_request_capture("unreachable.bmp");

  REQUIRE(bk__gfx_get_draw_count() == 2);

  // No app has been booted in this test binary, so bk_gpu() returns nullptr and
  // SDL_AcquireGPUCommandBuffer fails immediately -- this exercises bk__gfx_flush's
  // early-return path (command-buffer acquire failure) without needing a real
  // window/GPU device, and proves pending state doesn't survive it.
  bk__gfx_flush();

  REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
  REQUIRE(bk__gfx_get_pending_vertex_buffer() == nullptr);
  REQUIRE(bk__gfx_get_pending_index_buffer() == nullptr);
  REQUIRE(bk__gfx_get_pending_texture() == nullptr);
  REQUIRE(bk__gfx_get_pending_sampler() == nullptr);
  REQUIRE(bk__gfx_get_pending_canvas() == nullptr);
  REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "") == 0);
  // The draw chain must be cleared on this path too: bk__iterate calls bk__arena_reset
  // right after flush, so a surviving head would dangle into recycled arena memory.
  REQUIRE(bk__gfx_get_draw_count() == 0);
  REQUIRE(bk__gfx_get_draw_cmd(0) == nullptr);
}

static void test_bind_canvas_sets_pending_state(void) {
  static int dummy;
  BK_GfxCanvas *fake_canvas = (BK_GfxCanvas *)&dummy;

  bk_gfx_bind_canvas(fake_canvas);

  REQUIRE(bk__gfx_get_pending_canvas() == fake_canvas);
}

static void test_swapchain_depth_size_is_zero_until_created(void) {
  i32 width = -1, height = -1;
  bk__gfx_get_swapchain_depth_size(&width, &height);
  REQUIRE(width == 0);
  REQUIRE(height == 0);
}

static void test_swapchain_depth_shutdown_is_noop_when_never_created(void) {
  // No app has been booted in this test binary, so no flush ever ran and no
  // texture was ever created -- bk__gfx_shutdown must not crash regardless of
  // whether depth was configured enabled.
  bk__gfx_configure_swapchain_depth(true);
  bk__gfx_shutdown();

  i32 width = -1, height = -1;
  bk__gfx_get_swapchain_depth_size(&width, &height);
  REQUIRE(width == 0);
  REQUIRE(height == 0);

  bk__gfx_configure_swapchain_depth(false); // restore the default for other tests
}

static void test_bind_buffers_texture_and_draw_indexed_sets_pending_state(void) {
  static int dummy;
  BK_GfxBuffer *fake_vertex_buffer = (BK_GfxBuffer *)&dummy;
  BK_GfxBuffer *fake_index_buffer = (BK_GfxBuffer *)&dummy;
  BK_GfxTexture *fake_texture = (BK_GfxTexture *)&dummy;
  BK_GfxSampler *fake_sampler = (BK_GfxSampler *)&dummy;

  bk_gfx_bind_vertex_buffer(fake_vertex_buffer);
  bk_gfx_bind_index_buffer(fake_index_buffer);
  bk_gfx_bind_texture(fake_texture, fake_sampler);
  bk_gfx_draw_indexed(6);

  REQUIRE(bk__gfx_get_pending_vertex_buffer() == fake_vertex_buffer);
  REQUIRE(bk__gfx_get_pending_index_buffer() == fake_index_buffer);
  REQUIRE(bk__gfx_get_pending_texture() == fake_texture);
  REQUIRE(bk__gfx_get_pending_sampler() == fake_sampler);

  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(bk__gfx_get_draw_count() - 1);
  REQUIRE(draw != nullptr);
  REQUIRE(draw->index_count == 6);
  REQUIRE(draw->vertex_count == 0);
  // The record snapshotted the binds, rather than pointing at shared state.
  REQUIRE(draw->vertex_buffer == fake_vertex_buffer);
  REQUIRE(draw->index_buffer == fake_index_buffer);
  REQUIRE(draw->texture == fake_texture);
  REQUIRE(draw->sampler == fake_sampler);
}

int main(void) {
  test_default_clear_color();
  test_set_then_get_round_trips();
  test_last_set_wins();
  test_request_capture_sets_pending_path();
  test_bind_pipeline_and_draw_sets_pending_state();
  test_bind_canvas_sets_pending_state();
  test_swapchain_depth_size_is_zero_until_created();
  test_swapchain_depth_shutdown_is_noop_when_never_created();
  test_bind_buffers_texture_and_draw_indexed_sets_pending_state();
  test_flush_early_return_clears_pending_state();
  printf("test_gfx: OK\n");
  return 0;
}
