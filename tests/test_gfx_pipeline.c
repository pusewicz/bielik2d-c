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

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
  usize i = ((usize)y * (usize)width + (usize)x) * 4;
  REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
  REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
  REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
  REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
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
  const u8 *pixels = (const u8 *)pixels_buf;

  // Center: well inside the triangle (NDC bbox [-0.5,0.5] on both axes covers the
  // middle half of the viewport) -> solid red.
  s_check_pixel(pixels, size, size / 2, size / 2, 255, 0, 0, 255, tolerance);
  // Corners, inset by 1px: outside the triangle's bounding box under any backend's
  // NDC-to-pixel axis convention -> clear color (black).
  s_check_pixel(pixels, size, 1, 1, 0, 0, 0, 255, tolerance);
  s_check_pixel(pixels, size, size - 2, 1, 0, 0, 0, 255, tolerance);
  s_check_pixel(pixels, size, 1, size - 2, 0, 0, 0, 255, tolerance);
  s_check_pixel(pixels, size, size - 2, size - 2, 0, 0, 0, 255, tolerance);

  bk__free(pixels_buf);

  bk_gfx_pipeline_destroy(pipeline);
  s_free_shader(&vertex);
  s_free_shader(&fragment);
  SDL_ReleaseGPUTexture(device, offscreen);
  SDL_DestroyGPUDevice(device);
}

int main(void) {
  test_create_and_destroy_pipeline_succeeds();
  test_out_of_range_vertex_counts_return_null();
  test_draw_produces_expected_pixels();
  test_destroy_null_is_noop();
  printf("test_gfx_pipeline: OK\n");
  return 0;
}
