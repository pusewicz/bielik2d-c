#pragma once
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL_gpu.h>

/// Returns the color most recently set via bk_gfx_set_clear_color, or the
/// default {0.1, 0.1, 0.12, 1.0} if it hasn't been called yet.
BK_Color bk__gfx_get_clear_color(void);

/// Acquires the swapchain texture, clears it to the color set via
/// bk_gfx_set_clear_color, and presents. Called once per frame by the
/// frame pipeline. No-op (submits an empty command buffer) if the
/// swapchain texture isn't available (minimized/occluded window).
void bk__gfx_flush(void);

/// Test-only accessor: returns the pipeline bound via bk_gfx_bind_pipeline this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxPipeline *bk__gfx_get_pending_pipeline(void);

/// Test-only accessor: returns the vertex count set via bk_gfx_draw this frame, or
/// 0 if bk_gfx_draw hasn't been called since the last flush.
i32 bk__gfx_get_pending_vertex_count(void);

/// Test-only accessor: returns the buffer bound via bk_gfx_bind_vertex_buffer this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxBuffer *bk__gfx_get_pending_vertex_buffer(void);

/// Test-only accessor: returns the buffer bound via bk_gfx_bind_index_buffer this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxBuffer *bk__gfx_get_pending_index_buffer(void);

/// Test-only accessor: returns the texture bound via bk_gfx_bind_texture this frame,
/// or nullptr if none has been bound since the last flush.
BK_GfxTexture *bk__gfx_get_pending_texture(void);

/// Test-only accessor: returns the sampler bound via bk_gfx_bind_texture this frame,
/// or nullptr if none has been bound since the last flush.
BK_GfxSampler *bk__gfx_get_pending_sampler(void);

/// Test-only accessor: returns the index count set via bk_gfx_draw_indexed this
/// frame, or 0 if bk_gfx_draw_indexed hasn't been called since the last flush.
i32 bk__gfx_get_pending_index_count(void);

/// Test-only accessor: returns the path set via bk_gfx_request_capture this frame, or
/// an empty string if none has been requested since the last flush.
const char *bk__gfx_get_pending_capture_path(void);

/// Downloads width*height pixels (4 bytes/pixel) from texture via a copy pass added
/// to cmd, then submits cmd and waits for the GPU fence. cmd must not have been
/// submitted yet, and must not be used for anything else afterward -- this call
/// submits it on every path, success or failure. format must be
/// SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM or SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM (the
/// only 4-byte-per-pixel formats this helper supports; enforced by assertion, since
/// which format a caller passes is a programmer decision, not external data).
/// Returns a heap-allocated copy of the pixels (release with bk__free), or nullptr on
/// failure (logs via SDL_Log with a "BK: " prefix).
void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format);
