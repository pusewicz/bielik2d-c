#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL.h>

#include <stddef.h>
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

static void test_create_sampler_texture_and_upload_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxTexture *texture = bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
  REQUIRE(texture != nullptr);

  u32 pixels[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
  REQUIRE(bk_gfx_texture_upload(texture, pixels));

  bk_gfx_texture_destroy(texture);
  SDL_DestroyGPUDevice(device);
}

// Gate (spec §5/§9, plan Task 3): confirm SAMPLER | COMPUTE_STORAGE_WRITE actually
// creates on this machine's backend, and that R8G8B8A8_UNORM is a legal compute
// storage-write format -- both are load-bearing assumptions for samples/05_compute.
static void test_create_compute_target_texture_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxTexture *texture =
      bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, 16, 16);
  REQUIRE(texture != nullptr);

  bk_gfx_texture_destroy(texture);
  SDL_DestroyGPUDevice(device);
}

static void test_create_samplers_succeeds(void) {
  SDL_GPUDevice *device = s_create_device();

  BK_GfxSampler *nearest_clamp =
      bk_gfx_sampler_create(device, BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
  REQUIRE(nearest_clamp != nullptr);

  BK_GfxSampler *linear_repeat =
      bk_gfx_sampler_create(device, BK_GFX_FILTER_LINEAR, BK_GFX_ADDRESS_REPEAT);
  REQUIRE(linear_repeat != nullptr);

  bk_gfx_sampler_destroy(nearest_clamp);
  bk_gfx_sampler_destroy(linear_repeat);
  SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) {
  bk_gfx_texture_destroy(nullptr);
  bk_gfx_sampler_destroy(nullptr);
}

// ---------------------------------------------------------------------------
// Golden-image test: a textured, indexed quad. Proves buffers, textures,
// samplers, and indexed drawing all work together -- the headless counterpart
// to samples/04_textured_quad. No window or swapchain needed, same pattern as
// tests/test_gfx_pipeline.c's golden-image test.
// ---------------------------------------------------------------------------

typedef struct Vertex {
  f32 position[2];
  f32 uv[2];
  u8 color[4];
} Vertex;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);

  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
  void *data = SDL_LoadFile(path, out_size);
  REQUIRE(data != nullptr);
  return data;
}

static BK_GfxShaderDesc s_load_textured_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "textured.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "textured.%s.msl", stage);

  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  // textured.frag declares one sampler2D; textured.vert declares none. This count
  // must match the shader binary's actual declaration (SDL_GPU validates it).
  if (SDL_strcmp(stage, "fragment") == 0) {
    desc.num_samplers = 1;
  }
  return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
  usize i = ((usize)y * (usize)width + (usize)x) * 4;
  REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
  REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
  REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
  REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

// Checkerboard colors, hardcoded literals independent of the mutation test in
// test_draw_produces_expected_pixels_from_checkerboard below -- see the design spec
// §8 and the implementation plan Task 4 for why the assertions must not be re-derived
// from the uploaded array.
enum { RED_R = 255, RED_G = 0, RED_B = 0 };

enum { GREEN_R = 0, GREEN_G = 255, GREEN_B = 0 };

enum { BLUE_R = 0, BLUE_G = 0, BLUE_B = 255 };

enum { YELLOW_R = 255, YELLOW_G = 255, YELLOW_B = 0 };

static void test_draw_produces_expected_pixels_from_checkerboard(void) {
  constexpr int size = 64;
  constexpr int tolerance = 5;

  // Defensive, independent of test-function call order: see the identical call in
  // s_create_device above for why this is required.
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GPUDevice *device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
      nullptr);
  REQUIRE(device != nullptr);

  BK_GfxShaderDesc vertex_shader = s_load_textured_shader("vertex");
  BK_GfxShaderDesc fragment_shader = s_load_textured_shader("fragment");

  BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
  BK_GfxVertexAttribute vertex_attributes[3] = {
      {.location = 0,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
       .offset = offsetof(Vertex, position)},
      {.location = 1,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
       .offset = offsetof(Vertex, uv)      },
      {.location = 2,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
       .offset = offsetof(Vertex, color)   },
  };
  BK_GfxPipelineDesc pipeline_desc = {
      .vertex_shader = vertex_shader,
      .fragment_shader = fragment_shader,
      .vertex_buffers = &vertex_buffer_layout,
      .num_vertex_buffers = 1,
      .vertex_attributes = vertex_attributes,
      .num_vertex_attributes = 3,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
  };
  BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
  REQUIRE(pipeline != nullptr);

  // A full-viewport quad: NDC (-1,-1) is the lower-left corner (SDL_GPU's
  // documented coordinate system, SDL_gpu.h "Coordinate System"), which maps uv
  // (0,1) -- the texture's bottom-left texel row -- so screen and texture agree on
  // which edge is "down". White vertex color leaves the sampled texel unmodified.
  Vertex vertices[4] = {
      {.position = {-1, -1}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
      {.position = {1, -1},  .uv = {1, 1}, .color = {255, 255, 255, 255}},
      {.position = {-1, 1},  .uv = {0, 0}, .color = {255, 255, 255, 255}},
      {.position = {1, 1},   .uv = {1, 0}, .color = {255, 255, 255, 255}},
  };
  BK_GfxBuffer *vertex_buffer =
      bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
  REQUIRE(vertex_buffer != nullptr);
  REQUIRE(bk_gfx_buffer_upload(vertex_buffer, vertices, 0, sizeof vertices));

  u16 indices[6] = {0, 1, 2, 2, 1, 3};
  BK_GfxBuffer *index_buffer =
      bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_INDEX, sizeof indices);
  REQUIRE(index_buffer != nullptr);
  REQUIRE(bk_gfx_buffer_upload(index_buffer, indices, 0, sizeof indices));

  // 2x2 checkerboard, row-major top-to-bottom matching SDL_UploadToGPUTexture's
  // pixels_per_row/rows_per_layer convention: texel row 0 is the texture's top row
  // (uv v=0), so this lays out red/green on top, blue/yellow on the bottom.
  u8 checkerboard[2][2][4] = {
      {{RED_R, RED_G, RED_B, 255},    {GREEN_R, GREEN_G, GREEN_B, 255}   },
      {{BLUE_R, BLUE_G, BLUE_B, 255}, {YELLOW_R, YELLOW_G, YELLOW_B, 255}},
  };
  BK_GfxTexture *texture = bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
  REQUIRE(texture != nullptr);
  REQUIRE(bk_gfx_texture_upload(texture, checkerboard));

  BK_GfxSampler *sampler =
      bk_gfx_sampler_create(device, BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
  REQUIRE(sampler != nullptr);

  SDL_GPUTextureCreateInfo offscreen_info = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
      .width = size,
      .height = size,
      .layer_count_or_depth = 1,
      .num_levels = 1,
      .sample_count = SDL_GPU_SAMPLECOUNT_1,
  };
  SDL_GPUTexture *offscreen = SDL_CreateGPUTexture(device, &offscreen_info);
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
  SDL_GPUBufferBinding vertex_binding = {.buffer = bk__gfx_buffer_handle(vertex_buffer)};
  SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
  SDL_GPUBufferBinding index_binding = {.buffer = bk__gfx_buffer_handle(index_buffer)};
  SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
  SDL_GPUTextureSamplerBinding sampler_binding = {.texture = bk__gfx_texture_handle(texture),
                                                  .sampler = bk__gfx_sampler_handle(sampler)};
  SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);
  SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
  SDL_EndGPURenderPass(pass);

  void *pixels_buf = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                              SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  REQUIRE(pixels_buf != nullptr);
  const u8 *pixels = (const u8 *)pixels_buf;

  // Quadrant centers. See the coordinate-system comment on `vertices` above for why
  // top-left of the downloaded image is texel (0,0), etc. -- this is fully
  // determined by SDL_GPU's documented convention, not backend-dependent.
  s_check_pixel(pixels, size, size / 4, size / 4, RED_R, RED_G, RED_B, 255, tolerance);
  s_check_pixel(pixels, size, 3 * size / 4, size / 4, GREEN_R, GREEN_G, GREEN_B, 255, tolerance);
  s_check_pixel(pixels, size, size / 4, 3 * size / 4, BLUE_R, BLUE_G, BLUE_B, 255, tolerance);
  s_check_pixel(pixels, size, 3 * size / 4, 3 * size / 4, YELLOW_R, YELLOW_G, YELLOW_B, 255,
                tolerance);

  bk__free(pixels_buf);

  bk_gfx_pipeline_destroy(pipeline);
  bk_gfx_buffer_destroy(vertex_buffer);
  bk_gfx_buffer_destroy(index_buffer);
  bk_gfx_texture_destroy(texture);
  bk_gfx_sampler_destroy(sampler);
  s_free_shader(&vertex_shader);
  s_free_shader(&fragment_shader);
  SDL_ReleaseGPUTexture(device, offscreen);
  SDL_DestroyGPUDevice(device);
}

int main(void) {
  test_create_sampler_texture_and_upload_succeeds();
  test_create_compute_target_texture_succeeds();
  test_create_samplers_succeeds();
  test_destroy_null_is_noop();
  test_draw_produces_expected_pixels_from_checkerboard();
  printf("test_gfx_texture: OK\n");
  return 0;
}
