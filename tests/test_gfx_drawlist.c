#include "bk_test.h"
#include "internal/bk_gfx_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_math.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// Record-structure tests for the per-frame draw list. These need no GPU, no window and
// no booted app -- bk_frame_alloc creates its arena lazily -- so unlike the GPU replay
// tests in test_gfx_drawlist_gpu.c they are a *required* CI check on every platform,
// including Windows where no DXIL shader variant exists to create a pipeline with.
//
// They cover the failure mode most likely to be introduced here: a record that
// *references* the shared bind state instead of snapshotting it, which would make every
// draw in a frame replay with the frame's final state rather than its own.

static void test_each_draw_snapshots_its_own_state(void) {
  static int dummy_a, dummy_b;
  BK_GfxPipeline *pipeline = (BK_GfxPipeline *)&dummy_a;
  BK_GfxTexture *texture_a = (BK_GfxTexture *)&dummy_a;
  BK_GfxTexture *texture_b = (BK_GfxTexture *)&dummy_b;
  BK_GfxSampler *sampler = (BK_GfxSampler *)&dummy_a;

  bk__gfx_flush(); // no app booted, so this early-returns; used here to clear state
  REQUIRE(bk__gfx_get_draw_count() == 0);

  bk_gfx_bind_pipeline(pipeline);
  bk_gfx_bind_texture(texture_a, sampler);
  bk_gfx_draw(3);
  bk_gfx_bind_texture(texture_b, sampler);
  bk_gfx_draw(6);

  REQUIRE(bk__gfx_get_draw_count() == 2);

  // Each record kept the texture bound when *it* was recorded. If records referenced
  // shared state, both would report texture_b.
  const BK_GfxDrawCmd *first = bk__gfx_get_draw_cmd(0);
  const BK_GfxDrawCmd *second = bk__gfx_get_draw_cmd(1);
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(first->texture == texture_a);
  REQUIRE(second->texture == texture_b);

  // Order matches call order, not reverse.
  REQUIRE(first->vertex_count == 3);
  REQUIRE(second->vertex_count == 6);

  // Sticky state carries forward to later draws that don't rebind it.
  REQUIRE(first->pipeline == pipeline);
  REQUIRE(second->pipeline == pipeline);

  bk__gfx_flush();
}

static void test_flush_resets_the_chain(void) {
  static int dummy;
  bk__gfx_flush();

  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  bk_gfx_draw(3);
  bk_gfx_draw(3);
  bk_gfx_draw(3);
  REQUIRE(bk__gfx_get_draw_count() == 3);

  // The reset must happen even though this flush early-returns (no app booted, so
  // SDL_AcquireGPUCommandBuffer fails). bk__iterate calls bk__arena_reset immediately
  // after flush, so a chain surviving here would dangle into recycled arena memory.
  bk__gfx_flush();
  REQUIRE(bk__gfx_get_draw_count() == 0);
  REQUIRE(bk__gfx_get_draw_cmd(0) == nullptr);
  REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
}

static void test_draw_cmd_index_bounds(void) {
  static int dummy;
  bk__gfx_flush();
  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  bk_gfx_draw(3);

  REQUIRE(bk__gfx_get_draw_cmd(-1) == nullptr);
  REQUIRE(bk__gfx_get_draw_cmd(0) != nullptr);
  REQUIRE(bk__gfx_get_draw_cmd(1) == nullptr);
  REQUIRE(bk__gfx_get_draw_cmd(99) == nullptr);

  bk__gfx_flush();
}

static void test_indexed_and_instanced_counts_are_recorded(void) {
  static int dummy;
  bk__gfx_flush();

  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  bk_gfx_bind_index_buffer((BK_GfxBuffer *)&dummy);
  bk_gfx_draw_indexed(6);
  bk_gfx_draw(4);

  const BK_GfxDrawCmd *indexed = bk__gfx_get_draw_cmd(0);
  const BK_GfxDrawCmd *plain = bk__gfx_get_draw_cmd(1);
  REQUIRE(indexed != nullptr && plain != nullptr);
  // An indexed draw carries no vertex count and vice versa -- the replay picks the
  // SDL entry point off which one is non-zero.
  REQUIRE(indexed->index_count == 6);
  REQUIRE(indexed->vertex_count == 0);
  REQUIRE(plain->vertex_count == 4);
  REQUIRE(plain->index_count == 0);
  // Both default to a single instance.
  REQUIRE(indexed->instance_count == 1);
  REQUIRE(plain->instance_count == 1);

  bk__gfx_flush();
}

static void test_storage_buffer_slots_are_recorded(void) {
  static int dummy_a, dummy_b;
  bk__gfx_flush();

  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy_a);
  bk_gfx_bind_vertex_storage_buffer((BK_GfxBuffer *)&dummy_a, 0);
  bk_gfx_bind_vertex_storage_buffer((BK_GfxBuffer *)&dummy_b, 2);
  bk_gfx_bind_fragment_storage_buffer((BK_GfxBuffer *)&dummy_b, 1);
  bk_gfx_draw(3);

  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(0);
  REQUIRE(draw != nullptr);
  REQUIRE(draw->vertex_storage[0] == (BK_GfxBuffer *)&dummy_a);
  REQUIRE(draw->vertex_storage[2] == (BK_GfxBuffer *)&dummy_b);
  // Count is highest bound slot + 1, so the replay binds one contiguous run. Slot 1
  // was never bound and stays null inside that run.
  REQUIRE(draw->num_vertex_storage == 3);
  REQUIRE(draw->vertex_storage[1] == nullptr);
  REQUIRE(draw->num_fragment_storage == 2);
  REQUIRE(draw->fragment_storage[1] == (BK_GfxBuffer *)&dummy_b);

  bk__gfx_flush();
}

// Overwrites a chunk of stack below the current frame, to prove the uniform push kept
// its own copy rather than aliasing a caller local that has since gone out of scope.
static void s_clobber_stack(void) {
  volatile u8 scratch[512];
  for (usize i = 0; i < sizeof scratch / sizeof scratch[0]; ++i) {
    scratch[i] = 0xCD;
  }
}

// Pushes from a local that dies when this function returns. bk_gfx_push_vertex_uniform
// must copy into the frame arena -- SDL_PushGPUVertexUniformData doesn't run until
// flush, long after this frame is gone.
static void s_push_uniform_from_a_dead_frame(void) {
  f32 values[4] = {1.5f, -2.5f, 3.5f, 4.5f};
  bk_gfx_push_vertex_uniform(values, sizeof values);
}

static void test_uniform_data_is_copied_not_referenced(void) {
  static int dummy;
  bk__gfx_flush();

  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  s_push_uniform_from_a_dead_frame();
  s_clobber_stack();
  bk_gfx_draw(3);

  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(0);
  REQUIRE(draw != nullptr);
  REQUIRE(draw->vertex_uniform != nullptr);
  REQUIRE(draw->vertex_uniform_size == 4 * sizeof(f32));

  const f32 *stored = (const f32 *)draw->vertex_uniform;
  REQUIRE(stored[0] == 1.5f);
  REQUIRE(stored[1] == -2.5f);
  REQUIRE(stored[2] == 3.5f);
  REQUIRE(stored[3] == 4.5f);

  bk__gfx_flush();
}

static void test_uniform_pushes_are_per_stage(void) {
  static int dummy;
  bk__gfx_flush();

  f32 vertex_data = 1.0f;
  u32 fragment_data = 0xABCDEF01u;
  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  bk_gfx_push_vertex_uniform(&vertex_data, sizeof vertex_data);
  bk_gfx_push_fragment_uniform(&fragment_data, sizeof fragment_data);
  bk_gfx_draw(3);

  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(0);
  REQUIRE(draw != nullptr);
  REQUIRE(*(const f32 *)draw->vertex_uniform == 1.0f);
  REQUIRE(*(const u32 *)draw->fragment_uniform == 0xABCDEF01u);
  // Separate arena copies, not the same block reused.
  REQUIRE(draw->vertex_uniform != draw->fragment_uniform);

  bk__gfx_flush();
}

static void test_scissor_viewport_and_instance_count_are_recorded(void) {
  static int dummy;
  bk__gfx_flush();

  bk_gfx_bind_pipeline((BK_GfxPipeline *)&dummy);
  bk_gfx_set_scissor((BK_Rect){.x = 4, .y = 8, .width = 16, .height = 32});
  bk_gfx_set_viewport((BK_Rect){.x = 1, .y = 2, .width = 3, .height = 4});
  bk_gfx_draw_instanced(4, 7);

  const BK_GfxDrawCmd *draw = bk__gfx_get_draw_cmd(0);
  REQUIRE(draw != nullptr);
  REQUIRE(draw->scissor.x == 4);
  REQUIRE(draw->scissor.y == 8);
  REQUIRE(draw->scissor.width == 16);
  REQUIRE(draw->scissor.height == 32);
  REQUIRE(draw->viewport.width == 3);
  REQUIRE(draw->viewport.height == 4);
  REQUIRE(draw->vertex_count == 4);
  REQUIRE(draw->instance_count == 7);

  // A zero rect resets to "full target" -- the documented way to clear a scissor,
  // rather than a separate clear function.
  bk_gfx_set_scissor((BK_Rect){0});
  bk_gfx_draw(3);
  const BK_GfxDrawCmd *reset = bk__gfx_get_draw_cmd(1);
  REQUIRE(reset != nullptr);
  REQUIRE(reset->scissor.width == 0);
  REQUIRE(reset->scissor.height == 0);

  bk__gfx_flush();
}

int main(void) {
  test_each_draw_snapshots_its_own_state();
  test_flush_resets_the_chain();
  test_draw_cmd_index_bounds();
  test_indexed_and_instanced_counts_are_recorded();
  test_storage_buffer_slots_are_recorded();
  test_uniform_data_is_copied_not_referenced();
  test_uniform_pushes_are_per_stage();
  test_scissor_viewport_and_instance_count_are_recorded();
  printf("test_gfx_drawlist: OK\n");
  return 0;
}
