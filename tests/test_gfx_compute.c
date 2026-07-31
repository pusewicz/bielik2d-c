#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL.h>

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

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);

  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
  void *data = SDL_LoadFile(path, out_size);
  REQUIRE(data != nullptr);
  return data;
}

static BK_GfxComputePipelineDesc s_load_gradient_compute_desc(void) {
  BK_GfxComputePipelineDesc desc = {
      .num_readonly_storage_buffers = 1,
      .num_readwrite_storage_textures = 1,
      .threadcount_x = 8,
      .threadcount_y = 8,
      .threadcount_z = 1,
  };
  desc.spirv.code = s_load_shader_file("gradient.compute.spv", &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file("gradient.compute.msl", &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

static void s_free_compute_desc(BK_GfxComputePipelineDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

static void test_create_and_destroy_compute_pipeline_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxComputePipelineDesc desc = s_load_gradient_compute_desc();
  BK_GfxComputePipeline *pipeline = bk_gfx_compute_pipeline_create(device, &desc);
  REQUIRE(pipeline != nullptr);

  bk_gfx_compute_pipeline_destroy(pipeline);
  s_free_compute_desc(&desc);
  SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) {
  bk_gfx_compute_pipeline_destroy(nullptr);
}

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
  usize i = ((usize)y * (usize)width + (usize)x) * 4;
  REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
  REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
  REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
  REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

// Dispatches gradient.comp (reads a read-only storage buffer of {base_color, scale},
// writes color = base_color + scale * vec4(uv, 0, 0) to a read-write storage texture)
// and checks the result against hand-computed literal expected values -- proves
// buffer-as-storage-input, texture-as-storage-output, and the dispatch itself all
// work together. Readback reuses bk__gfx_download_texture; no new download plumbing.
static void test_dispatch_produces_expected_gradient(void) {
  constexpr int size = 16;
  constexpr int tolerance = 5;

  SDL_GPUDevice *device = s_create_device();

  BK_GfxComputePipelineDesc pipeline_desc = s_load_gradient_compute_desc();
  BK_GfxComputePipeline *pipeline = bk_gfx_compute_pipeline_create(device, &pipeline_desc);
  REQUIRE(pipeline != nullptr);

  // Params: base_color = (0.2, 0.3, 0.4, 1.0), scale = (0.5, 0.4, 0.0, 0.0). std140
  // layout: two vec4s, 16-byte aligned, no padding needed.
  f32 params[8] = {0.2f, 0.3f, 0.4f, 1.0f, 0.5f, 0.4f, 0.0f, 0.0f};
  BK_GfxBuffer *params_buffer =
      bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_STORAGE_READ, sizeof params);
  REQUIRE(params_buffer != nullptr);
  REQUIRE(bk_gfx_buffer_upload(params_buffer, params, 0, sizeof params));

  BK_GfxTexture *target =
      bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, size, size);
  REQUIRE(target != nullptr);

  BK_GfxBuffer *readonly_buffers[1] = {params_buffer};
  BK_GfxTexture *readwrite_textures[1] = {target};
  BK_GfxComputeDispatchDesc dispatch_desc = {
      .pipeline = pipeline,
      .readwrite_textures = readwrite_textures,
      .num_readwrite_textures = 1,
      .readonly_buffers = readonly_buffers,
      .num_readonly_buffers = 1,
      .groups_x = (size + 7) / 8,
      .groups_y = (size + 7) / 8,
      .groups_z = 1,
  };
  REQUIRE(bk_gfx_compute_dispatch(&dispatch_desc));

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  REQUIRE(cmd != nullptr);
  void *pixels_buf = bk__gfx_download_texture(device, cmd, bk__gfx_texture_handle(target), size,
                                              size, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  REQUIRE(pixels_buf != nullptr);
  const u8 *pixels = (const u8 *)pixels_buf;

  // uv = coord / (size - 1). color = base_color + scale * vec4(uv, 0, 0).
  // (0,0): uv=(0,0) -> (0.2, 0.3, 0.4, 1.0) -> (51, 77, 102, 255).
  s_check_pixel(pixels, size, 0, 0, 51, 77, 102, 255, tolerance);
  // (15,15): uv=(1,1) -> (0.7, 0.7, 0.4, 1.0) -> (178, 178, 102, 255).
  s_check_pixel(pixels, size, size - 1, size - 1, 178, 178, 102, 255, tolerance);
  // (15,0): uv=(1,0) -> (0.7, 0.3, 0.4, 1.0) -> (178, 77, 102, 255).
  s_check_pixel(pixels, size, size - 1, 0, 178, 77, 102, 255, tolerance);

  bk__free(pixels_buf);

  bk_gfx_compute_pipeline_destroy(pipeline);
  bk_gfx_buffer_destroy(params_buffer);
  bk_gfx_texture_destroy(target);
  s_free_compute_desc(&pipeline_desc);
  SDL_DestroyGPUDevice(device);
}

int main(void) {
  test_create_and_destroy_compute_pipeline_succeeds();
  test_destroy_null_is_noop();
  test_dispatch_produces_expected_gradient();
  printf("test_gfx_compute: OK\n");
  return 0;
}
