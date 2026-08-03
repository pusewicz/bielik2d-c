#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"

#include <bielik/bk_gfx_pipeline.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);

  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
  void *data = SDL_LoadFile(path, out_size);
  REQUIRE(data != nullptr);
  return data;
}

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

static void test_create_and_destroy_pipeline_succeeds(void) {
  // SDL_CreateGPUDevice requires the video subsystem initialized even though no
  // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
  // internally and errors "Video subsystem not initialized" otherwise).
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
  BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

  BK_GfxPipelineDesc desc = {
      .vertex_shader = vertex,
      .fragment_shader = fragment,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
  };

  BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &desc);
  REQUIRE(pipeline != nullptr);

  bk_gfx_pipeline_destroy(pipeline);
  s_free_shader(&vertex);
  s_free_shader(&fragment);
  SDL_DestroyGPUDevice(device);
}

static void test_every_blend_mode_creates_a_pipeline(void) {
  // Defensive, independent of test-function call order: see the identical call in
  // test_create_and_destroy_pipeline_succeeds above for why this is required.
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  const BK_GfxBlendMode modes[] = {
      BK_GFX_BLEND_NONE,     BK_GFX_BLEND_ALPHA,    BK_GFX_BLEND_PREMULTIPLIED,
      BK_GFX_BLEND_ADDITIVE, BK_GFX_BLEND_MULTIPLY, BK_GFX_BLEND_SCREEN,
  };
  for (usize i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");
    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = modes[i],
    };
    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &desc);
    REQUIRE(pipeline != nullptr);
    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
  }

  SDL_DestroyGPUDevice(device);
}

static void test_out_of_range_vertex_counts_return_null(void) {
  // Defensive, independent of test-function call order: see the identical call
  // in test_create_and_destroy_pipeline_succeeds above for why this is required.
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  // The bounds check runs before any shader is created, so no real shader
  // bytecode is needed here: an out-of-range count must fail before
  // desc.vertex_shader/fragment_shader are ever touched.
  BK_GfxPipelineDesc desc = {
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
  };

  desc.num_vertex_buffers = 9;
  REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

  desc.num_vertex_buffers = -1;
  REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

  desc.num_vertex_buffers = 0;
  desc.num_vertex_attributes = 17;
  REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

  desc.num_vertex_attributes = -1;
  REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

  SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) {
  bk_gfx_pipeline_destroy(nullptr);
}

// Standard point-in-triangle test via edge functions -- winding-independent (checks all
// three edge signs agree, rather than assuming a fixed CW/CCW order), matching what a
// rasterizer effectively computes at each sample point.
static bool s_point_in_triangle(f32 px, f32 py, f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
  f32 d1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  f32 d2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
  f32 d3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
  bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(has_neg && has_pos);
}

// Builds the exact rasterized reference for triangle.vert's hardcoded NDC triangle
// ((0,0.5), (0.5,-0.5), (-0.5,-0.5)), sampling each pixel at its center the way a
// rasterizer does. NDC -> pixel uses SDL_gpu.h's documented, backend-independent
// mapping (SDL_GPU normalizes Vulkan's opposite-Y-NDC convention away -- see
// "Coordinate System" in SDL_gpu.h): NDC (-1,-1) is the viewport's bottom-left; pixel
// (0,0) is the viewport's top-left with +Y down.
static SDL_Surface *s_build_triangle_reference(int size) {
  f32 fsize = (f32)size;
  f32 ax = (0.0f + 1.0f) * 0.5f * fsize, ay = (1.0f - 0.5f) * 0.5f * fsize;
  f32 bx = (0.5f + 1.0f) * 0.5f * fsize, by = (1.0f - -0.5f) * 0.5f * fsize;
  f32 cx = (-0.5f + 1.0f) * 0.5f * fsize, cy = (1.0f - -0.5f) * 0.5f * fsize;

  SDL_Surface *surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
  REQUIRE(surface != nullptr);
  REQUIRE(SDL_LockSurface(surface));
  u8 *pixels = (u8 *)surface->pixels;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      f32 sx = (f32)x + 0.5f, sy = (f32)y + 0.5f;
      bool inside = s_point_in_triangle(sx, sy, ax, ay, bx, by, cx, cy);
      u8 *px = pixels + (usize)y * (usize)surface->pitch + (usize)x * 4;
      px[0] = inside ? 255 : 0;
      px[1] = 0;
      px[2] = 0;
      px[3] = 255;
    }
  }
  SDL_UnlockSurface(surface);
  return surface;
}

static void test_draw_produces_expected_pixels(void) {
  constexpr int size = 64;
  constexpr int tolerance = 5;

  // Defensive, independent of test-function call order: see the identical call
  // in test_create_and_destroy_pipeline_succeeds above for why this is required.
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
  BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

  BK_GfxPipelineDesc pipeline_desc = {
      .vertex_shader = vertex,
      .fragment_shader = fragment,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
  };
  BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
  REQUIRE(pipeline != nullptr);

  SDL_GPUTextureCreateInfo texture_info = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
      .width = size,
      .height = size,
      .layer_count_or_depth = 1,
      .num_levels = 1,
      .sample_count = SDL_GPU_SAMPLECOUNT_1,
  };
  SDL_GPUTexture *offscreen = SDL_CreateGPUTexture(device, &texture_info);
  REQUIRE(offscreen != nullptr);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  REQUIRE(cmd != nullptr);

  SDL_GPUColorTargetInfo color_target = {
      .texture = offscreen,
      .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
      .load_op = SDL_GPU_LOADOP_CLEAR,
      .store_op = SDL_GPU_STOREOP_STORE,
  };
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pipeline));
  SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(pass);

  void *pixels_buf = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                              SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  REQUIRE(pixels_buf != nullptr);

  // SDLTest_CompareSurfaces only compares RGB (see its source), so the alpha channel
  // needs its own spot-check.
  REQUIRE_PIXEL(pixels_buf, size * 4, size / 2, size / 2, 255, 0, 0, 255, tolerance);

  // The triangle's three edges are the only unpredictable part -- Metal, Vulkan, and
  // D3D12 can legitimately pick a different pixel right on a rasterized edge.
  // max_failing_pixels is derived from the edge geometry, not guessed: the triangle in
  // pixel space is apex (32,16), base corners (16,48) and (48,48) (worked out from
  // triangle.vert's NDC coords via the same mapping s_build_triangle_reference uses) --
  // a 32px base plus two ~35.8px slanted sides, about 104px of edge in total. A 1px-wide
  // disagreement band along that perimeter is at most ~104 failing pixels; 100 stays
  // under that while remaining far below the triangle's interior area of 512px
  // (0.5*32*32), so a deleted, shrunk, or recolored triangle still fails by roughly 5x
  // the budget rather than slipping through it. Measured on this machine's Metal
  // backend: 0 pixels actually differ (this reference happens to match Metal's
  // rasterization exactly for this triangle) -- the 100px budget is headroom for
  // Vulkan/D3D12 backends this dev environment can't measure, not a fudge for an
  // observed failure.
  constexpr int allowable_error = 3;
  constexpr int max_failing_pixels = 100;
  SDL_Surface *actual =
      SDL_CreateSurfaceFrom(size, size, SDL_PIXELFORMAT_RGBA32, pixels_buf, size * 4);
  REQUIRE(actual != nullptr);
  SDL_Surface *reference = s_build_triangle_reference(size);
  REQUIRE_SURFACE(actual, reference, allowable_error, max_failing_pixels);
  SDL_DestroySurface(actual);
  SDL_DestroySurface(reference);

  bk__free(pixels_buf);

  bk_gfx_pipeline_destroy(pipeline);
  s_free_shader(&vertex);
  s_free_shader(&fragment);
  SDL_ReleaseGPUTexture(device, offscreen);
  SDL_DestroyGPUDevice(device);
}

int main(void) {
  test_create_and_destroy_pipeline_succeeds();
  test_every_blend_mode_creates_a_pipeline();
  test_out_of_range_vertex_counts_return_null();
  test_draw_produces_expected_pixels();
  test_destroy_null_is_noop();
  printf("test_gfx_pipeline: OK\n");
  return 0;
}
