#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_canvas_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_GPUDevice *s_create_device(void) {
  // SDL_CreateGPUDevice requires the video subsystem initialized even though no
  // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
  // internally and errors "Video subsystem not initialized" otherwise).
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);
  return device;
}

static void test_create_canvas_without_depth_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxCanvas *canvas =
      bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 64, .height = 32});
  REQUIRE(canvas != nullptr);

  i32 width = 0, height = 0;
  bk_gfx_canvas_size(canvas, &width, &height);
  REQUIRE_EQ_U64((uint64_t)width, 64);
  REQUIRE_EQ_U64((uint64_t)height, 32);

  BK_GfxTexture *color = bk_gfx_canvas_texture(canvas);
  REQUIRE(color != nullptr);
  REQUIRE(bk__gfx_texture_format(color) == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

  REQUIRE(bk__gfx_canvas_depth_handle(canvas) == nullptr);
  REQUIRE(bk__gfx_canvas_depth_format(canvas) == SDL_GPU_TEXTUREFORMAT_INVALID);

  bk_gfx_canvas_destroy(canvas);
  SDL_DestroyGPUDevice(device);
}

static void test_create_canvas_with_depth_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxCanvas *canvas = bk_gfx_canvas_create(
      device, &(BK_GfxCanvasDesc){.width = 16, .height = 16, .depth_stencil = true});
  REQUIRE(canvas != nullptr);

  REQUIRE(bk__gfx_canvas_depth_handle(canvas) != nullptr);
  REQUIRE(bk__gfx_canvas_depth_format(canvas) == bk_gfx_depth_stencil_format(device));

  bk_gfx_canvas_destroy(canvas);
  SDL_DestroyGPUDevice(device);
}

static void test_create_canvas_with_bad_dimensions_returns_null(void) {
  SDL_GPUDevice *device = s_create_device();

  REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 0, .height = 16}) == nullptr);
  REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 16, .height = 0}) == nullptr);
  REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = -1, .height = 16}) == nullptr);

  SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) {
  bk_gfx_canvas_destroy(nullptr);
}

// ---------------------------------------------------------------------------
// Golden-image test: depth testing actually discriminates. Renders two
// full-canvas-covering triangles at different depths into a real BK_GfxCanvas
// (not a hand-rolled SDL_CreateGPUTexture, so this also exercises the canvas's
// attachment pairing from bk_gfx_canvas_create), once with the near triangle
// drawn first and once with the far triangle drawn first. If depth testing is
// wired correctly, the near triangle wins everywhere both times; draw order alone
// would only prove that, not disprove a broken depth test.
// ---------------------------------------------------------------------------

typedef struct DepthVertex {
  float position[3];
  uint8_t color[4];
} DepthVertex;

static void *s_load_shader_file(const char *relative_path, size_t *out_size) {
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);

  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
  void *data = SDL_LoadFile(path, out_size);
  REQUIRE(data != nullptr);
  return data;
}

static BK_GfxShaderDesc s_load_depth_tri_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "depth_tri.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "depth_tri.%s.msl", stage);

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

static void s_check_pixel(const uint8_t *pixels, int width, int x, int y, uint8_t r, uint8_t g,
                          uint8_t b, uint8_t a, int tolerance) {
  size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
  REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
  REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
  REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
  REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

enum { NEAR_R = 255, NEAR_G = 0, NEAR_B = 0 }; // red triangle, z = 0.25 (nearer)

enum { FAR_R = 0, FAR_G = 0, FAR_B = 255 }; // blue triangle, z = 0.75 (farther)

// Draws both triangles as one 6-vertex TRIANGLE_LIST call, near_first controlling
// which occupies vertices 0-2 vs. 3-5, then downloads and checks the canvas. Both
// triangles use the standard over-sized-triangle trick (vertices reaching x/y = 3,
// clipped by the rasterizer) so each one alone covers the *entire* canvas -- every
// downloaded pixel must show the near triangle's color, not just a sampled few.
static void s_run_depth_order(SDL_GPUDevice *device, BK_GfxPipeline *pipeline, BK_GfxCanvas *canvas,
                              bool near_first, int size, int tolerance) {
  DepthVertex near_tri[3] = {
      {.position = {-1, -1, 0.25f}, .color = {NEAR_R, NEAR_G, NEAR_B, 255}},
      {.position = {3, -1, 0.25f},  .color = {NEAR_R, NEAR_G, NEAR_B, 255}},
      {.position = {-1, 3, 0.25f},  .color = {NEAR_R, NEAR_G, NEAR_B, 255}},
  };
  DepthVertex far_tri[3] = {
      {.position = {-1, -1, 0.75f}, .color = {FAR_R, FAR_G, FAR_B, 255}},
      {.position = {3, -1, 0.75f},  .color = {FAR_R, FAR_G, FAR_B, 255}},
      {.position = {-1, 3, 0.75f},  .color = {FAR_R, FAR_G, FAR_B, 255}},
  };

  DepthVertex vertices[6];
  if (near_first) {
    SDL_memcpy(vertices, near_tri, sizeof near_tri);
    SDL_memcpy(vertices + 3, far_tri, sizeof far_tri);
  } else {
    SDL_memcpy(vertices, far_tri, sizeof far_tri);
    SDL_memcpy(vertices + 3, near_tri, sizeof near_tri);
  }

  BK_GfxBuffer *vertex_buffer =
      bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
  REQUIRE(vertex_buffer != nullptr);
  REQUIRE(bk_gfx_buffer_upload(vertex_buffer, vertices, 0, sizeof vertices));

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  REQUIRE(cmd != nullptr);

  SDL_GPUTexture *color_handle = bk__gfx_texture_handle(bk_gfx_canvas_texture(canvas));
  SDL_GPUColorTargetInfo color_target = {
      .texture = color_handle,
      .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
      .load_op = SDL_GPU_LOADOP_CLEAR,
      .store_op = SDL_GPU_STOREOP_STORE,
  };
  SDL_GPUDepthStencilTargetInfo depth_target = {
      .texture = bk__gfx_canvas_depth_handle(canvas),
      .clear_depth = 1.0f,
      .load_op = SDL_GPU_LOADOP_CLEAR,
      .store_op = SDL_GPU_STOREOP_STORE,
      .stencil_load_op = SDL_GPU_LOADOP_CLEAR,
      .stencil_store_op = SDL_GPU_STOREOP_STORE,
  };
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
  SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pipeline));
  SDL_GPUBufferBinding vertex_binding = {.buffer = bk__gfx_buffer_handle(vertex_buffer)};
  SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
  SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
  SDL_EndGPURenderPass(pass);

  void *pixels_buf = bk__gfx_download_texture(device, cmd, color_handle, (Uint32)size, (Uint32)size,
                                              SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  REQUIRE(pixels_buf != nullptr);
  const uint8_t *pixels = (const uint8_t *)pixels_buf;

  s_check_pixel(pixels, size, size / 2, size / 2, NEAR_R, NEAR_G, NEAR_B, 255, tolerance);
  s_check_pixel(pixels, size, 2, 2, NEAR_R, NEAR_G, NEAR_B, 255, tolerance);
  s_check_pixel(pixels, size, size - 3, 2, NEAR_R, NEAR_G, NEAR_B, 255, tolerance);
  s_check_pixel(pixels, size, 2, size - 3, NEAR_R, NEAR_G, NEAR_B, 255, tolerance);
  s_check_pixel(pixels, size, size - 3, size - 3, NEAR_R, NEAR_G, NEAR_B, 255, tolerance);

  bk__free(pixels_buf);
  bk_gfx_buffer_destroy(vertex_buffer);
}

static void test_depth_test_picks_near_triangle_regardless_of_draw_order(void) {
  constexpr int size = 64;
  constexpr int tolerance = 5;

  // Defensive, independent of test-function call order: see the identical call in
  // s_create_device above for why this is required.
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  BK_GfxCanvas *canvas = bk_gfx_canvas_create(
      device, &(BK_GfxCanvasDesc){.width = size, .height = size, .depth_stencil = true});
  REQUIRE(canvas != nullptr);

  BK_GfxShaderDesc vertex_shader = s_load_depth_tri_shader("vertex");
  BK_GfxShaderDesc fragment_shader = s_load_depth_tri_shader("fragment");

  BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(DepthVertex)};
  BK_GfxVertexAttribute vertex_attributes[2] = {
      {.location = 0,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT3,
       .offset = offsetof(DepthVertex, position)},
      {.location = 1,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
       .offset = offsetof(DepthVertex, color)   },
  };
  BK_GfxPipelineDesc pipeline_desc = {
      .vertex_shader = vertex_shader,
      .fragment_shader = fragment_shader,
      .vertex_buffers = &vertex_buffer_layout,
      .num_vertex_buffers = 1,
      .vertex_attributes = vertex_attributes,
      .num_vertex_attributes = 2,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
      .depth_stencil_format = bk_gfx_depth_stencil_format(device),
      .depth_compare = BK_GFX_COMPARE_LESS,
      .depth_write = true,
  };
  BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
  REQUIRE(pipeline != nullptr);
  REQUIRE(bk__gfx_pipeline_depth_format(pipeline) == pipeline_desc.depth_stencil_format);

  s_run_depth_order(device, pipeline, canvas, /*near_first=*/true, size, tolerance);
  s_run_depth_order(device, pipeline, canvas, /*near_first=*/false, size, tolerance);

  bk_gfx_pipeline_destroy(pipeline);
  s_free_shader(&vertex_shader);
  s_free_shader(&fragment_shader);
  bk_gfx_canvas_destroy(canvas);
  SDL_DestroyGPUDevice(device);
}

int main(void) {
  test_create_canvas_without_depth_succeeds();
  test_create_canvas_with_depth_succeeds();
  test_create_canvas_with_bad_dimensions_returns_null();
  test_destroy_null_is_noop();
  test_depth_test_picks_near_triangle_regardless_of_draw_order();
  printf("test_gfx_canvas: OK\n");
  return 0;
}
