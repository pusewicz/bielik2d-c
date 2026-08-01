#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx_buffer.h>

#include <SDL3/SDL.h>

struct BK_GfxBuffer {
  SDL_GPUDevice *device;
  SDL_GPUBuffer *handle;
  u32 size;
};

static SDL_GPUBufferUsageFlags s_buffer_usage_flags(BK_GfxBufferUsage usage) {
  switch (usage) {
  case BK_GFX_BUFFER_USAGE_VERTEX:
    return SDL_GPU_BUFFERUSAGE_VERTEX;
  case BK_GFX_BUFFER_USAGE_INDEX:
    return SDL_GPU_BUFFERUSAGE_INDEX;
  case BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE:
    return SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
  case BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS:
    return SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
  }
  BK_ASSERT(false);
  return 0;
}

BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage, u32 size) {
  BK_ASSERT(device != nullptr);

  SDL_GPUBufferCreateInfo info = {
      .usage = s_buffer_usage_flags(usage),
      .size = size,
  };
  SDL_GPUBuffer *handle = SDL_CreateGPUBuffer(device, &info);
  if (handle == nullptr) {
    SDL_Log("BK: SDL_CreateGPUBuffer failed: %s", SDL_GetError());
    return nullptr;
  }

  BK_GfxBuffer *buffer = bk__alloc(sizeof(BK_GfxBuffer));
  if (buffer == nullptr) {
    SDL_ReleaseGPUBuffer(device, handle);
    return nullptr;
  }
  buffer->device = device;
  buffer->handle = handle;
  buffer->size = size;
  return buffer;
}

bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, u32 offset, u32 size) {
  BK_ASSERT(buffer != nullptr);
  BK_ASSERT(data != nullptr);

  if (offset > buffer->size || size > buffer->size - offset) {
    SDL_Log("BK: bk_gfx_buffer_upload: offset %u + size %u exceeds buffer size %u", offset, size,
            buffer->size);
    return false;
  }

  SDL_GPUTransferBufferCreateInfo transfer_info = {
      .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
      .size = size,
  };
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(buffer->device, &transfer_info);
  if (transfer == nullptr) {
    SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    return false;
  }

  void *mapped = SDL_MapGPUTransferBuffer(buffer->device, transfer, false);
  if (mapped == nullptr) {
    SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
    return false;
  }
  SDL_memcpy(mapped, data, size);
  SDL_UnmapGPUTransferBuffer(buffer->device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(buffer->device);
  if (cmd == nullptr) {
    SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
    return false;
  }

  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source = {.transfer_buffer = transfer, .offset = 0};
  SDL_GPUBufferRegion destination = {.buffer = buffer->handle, .offset = offset, .size = size};
  SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);

  SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
  return true;
}

void bk_gfx_buffer_destroy(BK_GfxBuffer *buffer) {
  if (buffer == nullptr) {
    return;
  }
  SDL_ReleaseGPUBuffer(buffer->device, buffer->handle);
  bk__free(buffer);
}

SDL_GPUBuffer *bk__gfx_buffer_handle(const BK_GfxBuffer *buffer) {
  BK_ASSERT(buffer != nullptr);
  return buffer->handle;
}

u32 bk__gfx_buffer_size(const BK_GfxBuffer *buffer) {
  BK_ASSERT(buffer != nullptr);
  return buffer->size;
}
