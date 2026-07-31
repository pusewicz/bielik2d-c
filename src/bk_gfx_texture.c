#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL.h>

struct BK_GfxTexture {
  SDL_GPUDevice *device;
  SDL_GPUTexture *handle;
  BK_GfxTextureUsage usage;
  SDL_GPUTextureFormat format;
  u32 width;
  u32 height;
};

struct BK_GfxSampler {
  SDL_GPUDevice *device;
  SDL_GPUSampler *handle;
};

BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage, i32 width,
                                     i32 height) {
  BK_ASSERT(device != nullptr);
  BK_ASSERT(width > 0);
  BK_ASSERT(height > 0);

  SDL_GPUTextureUsageFlags usage_flags = 0;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  switch (usage) {
  case BK_GFX_TEXTURE_USAGE_SAMPLER:
    usage_flags = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    break;
  case BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET:
    usage_flags = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    break;
  case BK_GFX_TEXTURE_USAGE_RENDER_TARGET:
    usage_flags = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    break;
  case BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL:
    usage_flags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    format = bk_gfx_depth_stencil_format(device);
    break;
  }

  SDL_GPUTextureCreateInfo info = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = format,
      .usage = usage_flags,
      .width = (Uint32)width,
      .height = (Uint32)height,
      .layer_count_or_depth = 1,
      .num_levels = 1,
      .sample_count = SDL_GPU_SAMPLECOUNT_1,
  };
  SDL_GPUTexture *handle = SDL_CreateGPUTexture(device, &info);
  if (handle == nullptr) {
    SDL_Log("BK: SDL_CreateGPUTexture failed: %s", SDL_GetError());
    return nullptr;
  }

  BK_GfxTexture *texture = bk__alloc(sizeof(BK_GfxTexture));
  if (texture == nullptr) {
    SDL_ReleaseGPUTexture(device, handle);
    return nullptr;
  }
  texture->device = device;
  texture->handle = handle;
  texture->usage = usage;
  texture->format = format;
  texture->width = (u32)width;
  texture->height = (u32)height;
  return texture;
}

bool bk_gfx_texture_upload(BK_GfxTexture *texture, const void *rgba_pixels) {
  BK_ASSERT(texture != nullptr);
  BK_ASSERT(rgba_pixels != nullptr);
  BK_ASSERT(texture->usage == BK_GFX_TEXTURE_USAGE_SAMPLER);

  u32 byte_size = texture->width * texture->height * 4;

  SDL_GPUTransferBufferCreateInfo transfer_info = {
      .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
      .size = byte_size,
  };
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(texture->device, &transfer_info);
  if (transfer == nullptr) {
    SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    return false;
  }

  void *mapped = SDL_MapGPUTransferBuffer(texture->device, transfer, false);
  if (mapped == nullptr) {
    SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
    return false;
  }
  SDL_memcpy(mapped, rgba_pixels, byte_size);
  SDL_UnmapGPUTransferBuffer(texture->device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(texture->device);
  if (cmd == nullptr) {
    SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
    return false;
  }

  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo source = {.transfer_buffer = transfer,
                                       .pixels_per_row = texture->width,
                                       .rows_per_layer = texture->height};
  SDL_GPUTextureRegion destination = {
      .texture = texture->handle, .w = texture->width, .h = texture->height, .d = 1};
  SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);

  SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
  return true;
}

void bk_gfx_texture_destroy(BK_GfxTexture *texture) {
  if (texture == nullptr) {
    return;
  }
  SDL_ReleaseGPUTexture(texture->device, texture->handle);
  bk__free(texture);
}

BK_GfxSampler *bk_gfx_sampler_create(SDL_GPUDevice *device, BK_GfxFilter filter,
                                     BK_GfxAddressMode address_mode) {
  BK_ASSERT(device != nullptr);

  SDL_GPUFilter sdl_filter =
      filter == BK_GFX_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
  SDL_GPUSamplerMipmapMode mipmap_mode = filter == BK_GFX_FILTER_LINEAR
                                             ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                             : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  SDL_GPUSamplerAddressMode sdl_address_mode = address_mode == BK_GFX_ADDRESS_REPEAT
                                                   ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                                                   : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  SDL_GPUSamplerCreateInfo info = {
      .min_filter = sdl_filter,
      .mag_filter = sdl_filter,
      .mipmap_mode = mipmap_mode,
      .address_mode_u = sdl_address_mode,
      .address_mode_v = sdl_address_mode,
      .address_mode_w = sdl_address_mode,
  };
  SDL_GPUSampler *handle = SDL_CreateGPUSampler(device, &info);
  if (handle == nullptr) {
    SDL_Log("BK: SDL_CreateGPUSampler failed: %s", SDL_GetError());
    return nullptr;
  }

  BK_GfxSampler *sampler = bk__alloc(sizeof(BK_GfxSampler));
  if (sampler == nullptr) {
    SDL_ReleaseGPUSampler(device, handle);
    return nullptr;
  }
  sampler->device = device;
  sampler->handle = handle;
  return sampler;
}

void bk_gfx_sampler_destroy(BK_GfxSampler *sampler) {
  if (sampler == nullptr) {
    return;
  }
  SDL_ReleaseGPUSampler(sampler->device, sampler->handle);
  bk__free(sampler);
}

SDL_GPUTexture *bk__gfx_texture_handle(const BK_GfxTexture *texture) {
  BK_ASSERT(texture != nullptr);
  return texture->handle;
}

SDL_GPUSampler *bk__gfx_sampler_handle(const BK_GfxSampler *sampler) {
  BK_ASSERT(sampler != nullptr);
  return sampler->handle;
}

SDL_GPUTextureFormat bk__gfx_texture_format(const BK_GfxTexture *texture) {
  BK_ASSERT(texture != nullptr);
  return texture->format;
}
