#pragma once
#include <bielik/bk_types.h>

#include <SDL3/SDL_gpu.h>

/// What a buffer is used for. Exclusive, not a bitmask -- SDL_GPU itself rejects a
/// buffer created with both VERTEX and INDEX usage, and nothing in 2D needs a buffer
/// that is more than one thing at once.
typedef enum BK_GfxBufferUsage {
  BK_GFX_BUFFER_USAGE_VERTEX,
  BK_GFX_BUFFER_USAGE_INDEX,        // 16-bit indices; bk_gfx_bind_index_buffer hardcodes
                                    // the element size, see bk_gfx.h
  BK_GFX_BUFFER_USAGE_STORAGE_READ, // read-only storage buffer in a compute shader
} BK_GfxBufferUsage;

/// A GPU buffer: vertex data, index data, or compute storage input. Owns its device
/// upload machinery internally; callers just create, upload, bind (via bk_gfx), and
/// destroy.
typedef struct BK_GfxBuffer BK_GfxBuffer;

/// Creates a buffer of the given usage and byte size. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on SDL_GPU failure. device is explicit (not the
/// bk_gpu() singleton), matching bk_gfx_pipeline_create's precedent -- testable with
/// no window or running app.
BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage, u32 size);

/// Uploads size bytes from data into buffer starting at byte offset offset. Returns
/// false and logs via SDL_Log ("BK: " prefix) if offset + size exceeds the buffer's
/// size -- a runtime-data-dependent failure, not a programmer-error precondition, so
/// it's a recoverable return rather than an assert. buffer and data must be non-null
/// (BK_ASSERT).
bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, u32 offset, u32 size);

/// Destroys a buffer. No-op if buffer is nullptr.
void bk_gfx_buffer_destroy(BK_GfxBuffer *buffer);
