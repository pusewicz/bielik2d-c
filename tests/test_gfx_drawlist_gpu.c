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

// GPU-dependent draw-list tests: end-to-end replay through a real window and device.
// Split out of test_gfx_drawlist.c so the record-structure tests there stay a required
// CI check -- everything here needs a pipeline, which cannot be created on Windows
// while the shader toolchain ships SPIR-V and MSL but no DXIL (see DEVIATIONS.md), so
// this binary runs in CI's allow-failure GPU group alongside the other GPU tests.
//
// End-to-end replay through a real window and GPU device. Two overlapping
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
static bool s_scissor_mode = false;     // draw one clipped quad instead of two
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

  if (s_scissor_mode) {
    // One full-target quad, clipped to the left half. The right half must keep the
    // clear color -- proving the scissor was applied, not ignored.
    bk_gfx_set_scissor((BK_Rect){.x = 0, .y = 0, .width = 32, .height = 64});
    bk_gfx_bind_vertex_buffer(s_first_quad);
    bk_gfx_draw(6);
    REQUIRE(bk__gfx_get_draw_count() == 1);
  } else {
    // Two draws, one frame -- the whole point. Both cover the full target, so the one
    // recorded second must be the one visible.
    BK_GfxBuffer *first = s_second_drawn_last ? s_first_quad : s_second_quad;
    BK_GfxBuffer *second = s_second_drawn_last ? s_second_quad : s_first_quad;
    bk_gfx_bind_vertex_buffer(first);
    bk_gfx_draw(6);
    bk_gfx_bind_vertex_buffer(second);
    bk_gfx_draw(6);
    REQUIRE(bk__gfx_get_draw_count() == 2);
  }
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
  SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_drawlist_gpu_output.bmp",
               base_path);
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
  s_scissor_mode = false;
  // Both directions. Asserting only one would pass against a backwards replay.
  if (!s_run_and_check(true, SECOND_R, SECOND_G, SECOND_B)) {
    return;
  }
  REQUIRE(s_run_and_check(false, FIRST_R, FIRST_G, FIRST_B));
}

// Runs the app in scissor mode and checks a pixel inside the scissored region and one
// outside it. Same skip-on-no-capture contract as s_run_and_check.
static void test_scissor_clips_the_rendered_frame(void) {
  s_scissor_mode = true;
  s_frames = 0;

  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);
  SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_drawlist_scissor.bmp", base_path);
  SDL_RemovePath(s_capture_path);

  BK_AppDesc desc = {
      .window = {.title = "test_gfx_drawlist_scissor", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .init = test_init,
      .update = test_update,
      .render = test_render,
      .quit = test_quit,
  };
  bk_run(&desc, 0, nullptr);

  SDL_Surface *surface = SDL_LoadBMP(s_capture_path);
  if (surface == nullptr) {
    printf("test_gfx_drawlist: no scissor capture produced, skipping pixel check\n");
    s_scissor_mode = false;
    return;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  REQUIRE(rgba != nullptr);

  const u8 *pixels = (const u8 *)rgba->pixels;
  constexpr int tolerance = 8;

  // Inside the scissor (left quarter): the quad's color.
  const u8 *inside = pixels + ((rgba->h / 2) * rgba->pitch) + (rgba->w / 4) * 4;
  REQUIRE(SDL_abs((int)inside[0] - (int)FIRST_R) <= tolerance);
  REQUIRE(SDL_abs((int)inside[1] - (int)FIRST_G) <= tolerance);
  REQUIRE(SDL_abs((int)inside[2] - (int)FIRST_B) <= tolerance);

  // Outside it (right quarter): untouched clear color. Without this half the test
  // would pass against a scissor that was silently ignored.
  const u8 *outside = pixels + ((rgba->h / 2) * rgba->pitch) + (rgba->w * 3 / 4) * 4;
  REQUIRE(outside[0] <= tolerance);
  REQUIRE(outside[1] <= tolerance);
  REQUIRE(outside[2] <= tolerance);

  SDL_DestroySurface(rgba);
  SDL_RemovePath(s_capture_path);
  s_scissor_mode = false;
}

// Part 3: storage buffer + uniform + instancing, end to end on the GPU. This is the
// only check that the MSL binding remap (see cmake/shaders.cmake) is actually right:
// a wrong [[buffer]] index doesn't fail at pipeline creation, it drops the command
// buffer at draw time and the capture reads back as the clear color.

typedef struct InstQuad {
  f32 center[2];
  f32 half_size[2];
  f32 color[4];
} InstQuad;

typedef struct InstCamera {
  f32 basis[4];
  f32 origin[4];
} InstCamera;

static BK_GfxPipeline *s_inst_pipeline = nullptr;
static BK_GfxBuffer *s_inst_quads = nullptr;

static BK_GfxShaderDesc s_load_named_shader(const char *name, const char *stage) {
  char spv[64], msl[64];
  SDL_snprintf(spv, sizeof spv, "%s.%s.spv", name, stage);
  SDL_snprintf(msl, sizeof msl, "%s.%s.msl", name, stage);
  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

static BK_Result inst_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;

  BK_GfxShaderDesc vertex = s_load_named_shader("instanced", "vertex");
  BK_GfxShaderDesc fragment = s_load_named_shader("instanced", "fragment");
  vertex.num_storage_buffers = 1;
  vertex.num_uniform_buffers = 1;

  BK_GfxPipelineDesc desc = {
      .vertex_shader = vertex,
      .fragment_shader = fragment,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_STRIP,
      .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
      .blend_mode = BK_GFX_BLEND_NONE,
  };
  s_inst_pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);
  REQUIRE(s_inst_pipeline != nullptr);
  SDL_free((void *)vertex.spirv.code);
  SDL_free((void *)vertex.msl.code);
  SDL_free((void *)fragment.spirv.code);
  SDL_free((void *)fragment.msl.code);

  // Three small quads at the left, center, and right thirds of a 64-unit world, with
  // gaps between them. The gaps are what make this test discriminating: a shader that
  // ignored gl_InstanceIndex would stack all three at one spot and leave them empty.
  InstQuad quads[3] = {
      {.center = {-21.0f, 0.0f}, .half_size = {6.0f, 24.0f}, .color = {1, 0, 0, 1}},
      {.center = {0.0f, 0.0f},   .half_size = {6.0f, 24.0f}, .color = {0, 1, 0, 1}},
      {.center = {21.0f, 0.0f},  .half_size = {6.0f, 24.0f}, .color = {0, 0, 1, 1}},
  };
  s_inst_quads = bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, sizeof quads);
  REQUIRE(s_inst_quads != nullptr);
  REQUIRE(bk_gfx_buffer_upload(s_inst_quads, quads, 0, sizeof quads));
  return BK_CONTINUE;
}

static void inst_render(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_gfx_set_clear_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});

  BK_M3x2 mvp = bk_m3x2_ortho(64.0f, 64.0f);
  InstCamera camera = {
      .basis = {mvp.x.x,      mvp.x.y,      mvp.y.x, mvp.y.y},
      .origin = {mvp.origin.x, mvp.origin.y, 0.0f,    0.0f   },
  };

  bk_gfx_bind_pipeline(s_inst_pipeline);
  bk_gfx_bind_vertex_storage_buffer(s_inst_quads, 0);
  bk_gfx_push_vertex_uniform(&camera, sizeof camera);
  bk_gfx_draw_instanced(4, 3);
  bk_gfx_request_capture(s_capture_path);
}

static void inst_quit(void *state, BK_Result result) {
  (void)state;
  (void)result;
  bk_gfx_buffer_destroy(s_inst_quads);
  bk_gfx_pipeline_destroy(s_inst_pipeline);
  s_inst_quads = nullptr;
  s_inst_pipeline = nullptr;
}

static void test_instancing_places_each_instance_separately(void) {
  s_frames = 0;
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);
  SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_drawlist_instanced.bmp",
               base_path);
  SDL_RemovePath(s_capture_path);

  BK_AppDesc desc = {
      .window = {.title = "test_gfx_drawlist_instanced", .width = 64, .height = 64},
      .time = {.tick_hz = 60},
      .init = inst_init,
      .update = test_update,
      .render = inst_render,
      .quit = inst_quit,
  };
  bk_run(&desc, 0, nullptr);

  SDL_Surface *surface = SDL_LoadBMP(s_capture_path);
  if (surface == nullptr) {
    printf("test_gfx_drawlist: no instanced capture produced, skipping pixel check\n");
    return;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  REQUIRE(rgba != nullptr);

  const u8 *pixels = (const u8 *)rgba->pixels;
  const int mid_row = rgba->h / 2;
  constexpr int tolerance = 12;

  // World x maps to pixels via the 64-wide ortho: -21 -> ~11px, 0 -> 32px, +21 -> ~53px.
  const u8 *left = pixels + mid_row * rgba->pitch + 11 * 4;
  const u8 *center = pixels + mid_row * rgba->pitch + 32 * 4;
  const u8 *right = pixels + mid_row * rgba->pitch + 53 * 4;
  REQUIRE(left[0] > 255 - tolerance && left[1] < tolerance);     // red instance
  REQUIRE(center[1] > 255 - tolerance && center[0] < tolerance); // green instance
  REQUIRE(right[2] > 255 - tolerance && right[0] < tolerance);   // blue instance

  // The gaps stay background. Without these, three quads stacked at one spot -- a
  // shader ignoring gl_InstanceIndex -- would still pass the three checks above.
  const u8 *gap_left = pixels + mid_row * rgba->pitch + 22 * 4;
  const u8 *gap_right = pixels + mid_row * rgba->pitch + 43 * 4;
  REQUIRE(gap_left[0] <= tolerance && gap_left[1] <= tolerance && gap_left[2] <= tolerance);
  REQUIRE(gap_right[0] <= tolerance && gap_right[1] <= tolerance && gap_right[2] <= tolerance);

  SDL_DestroySurface(rgba);
  SDL_RemovePath(s_capture_path);
}

int main(void) {
  test_replay_order_matches_record_order();
  test_scissor_clips_the_rendered_frame();
  test_instancing_places_each_instance_separately();
  printf("test_gfx_drawlist_gpu: OK\n");
  return 0;
}
