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

// Builds the exact reference image for the gradient dispatch: per-texel, the same f32
// expression gradient.comp computes (uv = coord / (size - 1); color = base_color +
// scale * vec4(uv, 0, 0)), quantized the way a UNORM imageStore does -- round to
// nearest, not truncate. This is deterministic per-texel math with no rasterization or
// sampling involved, so the whole image should match the GPU's output exactly, modulo
// float rounding at exact half-integer boundaries (see test_dispatch_produces_expected_
// gradient's allowable_error comment).
static SDL_Surface *s_build_gradient_reference(int size, const f32 base_color[4],
                                               const f32 scale[4]) {
  SDL_Surface *surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
  REQUIRE(surface != nullptr);
  REQUIRE(SDL_LockSurface(surface));
  u8 *pixels = (u8 *)surface->pixels;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      f32 uv_x = (f32)x / (f32)(size - 1);
      f32 uv_y = (f32)y / (f32)(size - 1);
      f32 r = base_color[0] + scale[0] * uv_x;
      f32 g = base_color[1] + scale[1] * uv_y;
      f32 b = base_color[2] + scale[2] * 0.0f;
      f32 a = base_color[3] + scale[3] * 0.0f;
      u8 *px = pixels + (usize)y * (usize)surface->pitch + (usize)x * 4;
      px[0] = (u8)SDL_lroundf(r * 255.0f);
      px[1] = (u8)SDL_lroundf(g * 255.0f);
      px[2] = (u8)SDL_lroundf(b * 255.0f);
      px[3] = (u8)SDL_lroundf(a * 255.0f);
    }
  }
  SDL_UnlockSurface(surface);
  return surface;
}

// Dispatches gradient.comp (reads a read-only storage buffer of {base_color, scale},
// writes color = base_color + scale * vec4(uv, 0, 0) to a read-write storage texture)
// and checks the whole result against a CPU-computed reference image -- proves
// buffer-as-storage-input, texture-as-storage-output, and the dispatch itself all
// work together, over every texel rather than three samples. Readback reuses
// bk__gfx_download_texture; no new download plumbing.
static void test_dispatch_produces_expected_gradient(void) {
  constexpr int size = 16;
  // allowable_error absorbs float-rounding noise at exact half-integer boundaries
  // (e.g. (15,15)'s R and G channels both land on exactly x.5 -- see the params
  // comment below): +-1 per RGB channel, sum of squares 1+1+1=3. That noise is
  // uniform across the image (every texel's math can land on a boundary), which is
  // exactly what allowable_error is for -- see REQUIRE_SURFACE's doc comment.
  // max_failing_pixels=0: no rasterization or sampling happens here, so unlike the
  // other converted tests there is no edge band to budget for -- a pixel exceeding
  // allowable_error is a real mismatch.
  constexpr int allowable_error = 3;
  constexpr int max_failing_pixels = 0;

  SDL_GPUDevice *device = s_create_device();

  BK_GfxComputePipelineDesc pipeline_desc = s_load_gradient_compute_desc();
  BK_GfxComputePipeline *pipeline = bk_gfx_compute_pipeline_create(device, &pipeline_desc);
  REQUIRE(pipeline != nullptr);

  // Params: base_color = (0.2, 0.3, 0.4, 1.0), scale = (0.5, 0.4, 0.0, 0.0). std140
  // layout: two vec4s, 16-byte aligned, no padding needed.
  f32 params[8] = {0.2f, 0.3f, 0.4f, 1.0f, 0.5f, 0.4f, 0.0f, 0.0f};
  BK_GfxBuffer *params_buffer =
      bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE, sizeof params);
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

  // SDLTest_CompareSurfaces only compares RGB (see its source), so the alpha channel
  // needs its own spot-check -- params.base_color.a=1.0, scale.a=0.0, so every texel's
  // alpha should be exactly 255 regardless of uv.
  REQUIRE_PIXEL(pixels_buf, size * 4, 0, 0, 51, 77, 102, 255, 1);

  SDL_Surface *actual =
      SDL_CreateSurfaceFrom(size, size, SDL_PIXELFORMAT_RGBA32, pixels_buf, size * 4);
  REQUIRE(actual != nullptr);
  const f32 base_color[4] = {params[0], params[1], params[2], params[3]};
  const f32 scale[4] = {params[4], params[5], params[6], params[7]};
  SDL_Surface *reference = s_build_gradient_reference(size, base_color, scale);
  REQUIRE_SURFACE(actual, reference, allowable_error, max_failing_pixels);
  SDL_DestroySurface(actual);
  SDL_DestroySurface(reference);

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
