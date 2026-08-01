#include "bk_test.h"
#include "internal/bk_gfx_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// Part 1: record-structure tests. These need no GPU, no window, and no booted app --
// bk_frame_alloc creates its arena lazily. They cover the failure mode most likely to
// be introduced here: a record that *references* the shared bind state instead of
// snapshotting it, which would make every draw in a frame replay with the frame's final
// state rather than its own.

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

// Part 2: end-to-end replay through a real window and GPU device. Two overlapping
// full-screen quads are drawn in one frame; the second must win at the overlap. The
// frame is captured to a BMP and read back, and the whole thing runs twice with the
// draw order swapped -- a single-order check passes against a replay that walks the
// chain backwards.

typedef struct ColorVertex {
  f32 position[3];
  u8 color[4];
} ColorVertex;

constexpr u8 FIRST_R = 200, FIRST_G = 40, FIRST_B = 40;
constexpr u8 SECOND_R = 40, SECOND_G = 60, SECOND_B = 200;

static bool s_second_drawn_last = true; // which color should win
static char s_capture_path[512];
static int s_frames = 0;

static BK_GfxPipeline *s_pipeline = nullptr;
static BK_GfxBuffer *s_first_quad = nullptr;
static BK_GfxBuffer *s_second_quad = nullptr;

static void *s_load_shader_file(const char *name, usize *out_size) {
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);
  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, name);
  void *data = SDL_LoadFile(path, out_size);
  REQUIRE(data != nullptr);
  return data;
}

static BK_GfxShaderDesc s_load_shader(const char *stage) {
  char spv[64], msl[64];
  SDL_snprintf(spv, sizeof spv, "depth_tri.%s.spv", stage);
  SDL_snprintf(msl, sizeof msl, "depth_tri.%s.msl", stage);
  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

// A full-target quad as two triangles, at z = 0 with depth testing off.
static BK_GfxBuffer *s_make_quad(u8 red, u8 green, u8 blue) {
  ColorVertex verts[6] = {
      {.position = {-1, -1, 0}, .color = {red, green, blue, 255}},
      {.position = {1, -1, 0},  .color = {red, green, blue, 255}},
      {.position = {-1, 1, 0},  .color = {red, green, blue, 255}},
      {.position = {1, -1, 0},  .color = {red, green, blue, 255}},
      {.position = {1, 1, 0},   .color = {red, green, blue, 255}},
      {.position = {-1, 1, 0},  .color = {red, green, blue, 255}},
  };
  BK_GfxBuffer *buffer = bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof verts);
  REQUIRE(buffer != nullptr);
  REQUIRE(bk_gfx_buffer_upload(buffer, verts, 0, sizeof verts));
  return buffer;
}

static BK_Result test_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;

  BK_GfxShaderDesc vertex = s_load_shader("vertex");
  BK_GfxShaderDesc fragment = s_load_shader("fragment");
  BK_GfxVertexBufferLayout layout = {.slot = 0, .pitch = sizeof(ColorVertex)};
  BK_GfxVertexAttribute attributes[2] = {
      {.location = 0,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT3,
       .offset = offsetof(ColorVertex, position)},
      {.location = 1,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
       .offset = offsetof(ColorVertex, color)   },
  };
  BK_GfxPipelineDesc desc = {
      .vertex_shader = vertex,
      .fragment_shader = fragment,
      .vertex_buffers = &layout,
      .num_vertex_buffers = 1,
      .vertex_attributes = attributes,
      .num_vertex_attributes = 2,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
      .blend_mode = BK_GFX_BLEND_NONE,
  };
  s_pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);
  REQUIRE(s_pipeline != nullptr);
  SDL_free((void *)vertex.spirv.code);
  SDL_free((void *)vertex.msl.code);
  SDL_free((void *)fragment.spirv.code);
  SDL_free((void *)fragment.msl.code);

  s_first_quad = s_make_quad(FIRST_R, FIRST_G, FIRST_B);
  s_second_quad = s_make_quad(SECOND_R, SECOND_G, SECOND_B);
  return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  s_frames++;
  return s_frames >= 3 ? BK_DONE : BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_gfx_set_clear_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});
  bk_gfx_bind_pipeline(s_pipeline);

  // Two draws, one frame -- the whole point. Both cover the full target, so the one
  // recorded second must be the one visible.
  BK_GfxBuffer *first = s_second_drawn_last ? s_first_quad : s_second_quad;
  BK_GfxBuffer *second = s_second_drawn_last ? s_second_quad : s_first_quad;
  bk_gfx_bind_vertex_buffer(first);
  bk_gfx_draw(6);
  bk_gfx_bind_vertex_buffer(second);
  bk_gfx_draw(6);

  REQUIRE(bk__gfx_get_draw_count() == 2);
  bk_gfx_request_capture(s_capture_path);
}

static void test_quit(void *state, BK_Result result) {
  (void)state;
  (void)result;
  bk_gfx_buffer_destroy(s_first_quad);
  bk_gfx_buffer_destroy(s_second_quad);
  bk_gfx_pipeline_destroy(s_pipeline);
  s_first_quad = nullptr;
  s_second_quad = nullptr;
  s_pipeline = nullptr;
}

// Runs the app once and asserts the captured frame's center pixel is the expected
// color. Returns false if the capture never appeared (no GPU in this environment),
// which the caller treats as a skip rather than a failure -- consistent with the other
// GPU-dependent tests being continue-on-error in CI.
static bool s_run_and_check(bool second_last, u8 red, u8 green, u8 blue) {
  s_second_drawn_last = second_last;
  s_frames = 0;

  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);
  SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_drawlist_output.bmp", base_path);
  SDL_RemovePath(s_capture_path);

  BK_AppDesc desc = {
      .window = {.title = "test_gfx_drawlist", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .init = test_init,
      .update = test_update,
      .render = test_render,
      .quit = test_quit,
  };
  bk_run(&desc, 0, nullptr);

  SDL_Surface *surface = SDL_LoadBMP(s_capture_path);
  if (surface == nullptr) {
    printf("test_gfx_drawlist: no capture produced (%s), skipping pixel check\n", SDL_GetError());
    return false;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  REQUIRE(rgba != nullptr);

  const u8 *pixels = (const u8 *)rgba->pixels;
  const u8 *center = pixels + ((rgba->h / 2) * rgba->pitch) + (rgba->w / 2) * 4;
  constexpr int tolerance = 8;
  REQUIRE(SDL_abs((int)center[0] - (int)red) <= tolerance);
  REQUIRE(SDL_abs((int)center[1] - (int)green) <= tolerance);
  REQUIRE(SDL_abs((int)center[2] - (int)blue) <= tolerance);

  SDL_DestroySurface(rgba);
  SDL_RemovePath(s_capture_path);
  return true;
}

static void test_replay_order_matches_record_order(void) {
  // Both directions. Asserting only one would pass against a backwards replay.
  if (!s_run_and_check(true, SECOND_R, SECOND_G, SECOND_B)) {
    return;
  }
  REQUIRE(s_run_and_check(false, FIRST_R, FIRST_G, FIRST_B));
}

int main(void) {
  test_each_draw_snapshots_its_own_state();
  test_flush_resets_the_chain();
  test_draw_cmd_index_bounds();
  test_indexed_and_instanced_counts_are_recorded();
  test_storage_buffer_slots_are_recorded();
  test_uniform_data_is_copied_not_referenced();
  test_uniform_pushes_are_per_stage();
  test_replay_order_matches_record_order();
  printf("test_gfx_drawlist: OK\n");
  return 0;
}
