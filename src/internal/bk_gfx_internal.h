#pragma once
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL_gpu.h>

/// Storage buffer slots bindable per shader stage. Four is what CF's tiled path needs
/// (cmds, payload, tiles, list); the instanced path uses two.
constexpr i32 BK_GFX_MAX_STORAGE_BUFFERS = 4;

/// One recorded draw: a snapshot of the bind state at the moment bk_gfx_draw* was
/// called, plus that draw's own counts. Allocated from the frame arena and chained in
/// call order; bk__gfx_flush walks the chain. The same type doubles as the "currently
/// bound state" a draw snapshots from, so recording a draw is one struct copy.
///
/// Deliberately has no canvas member: canvas targeting is frame-level (one canvas per
/// flush), and a per-record field would imply per-draw targeting the replay loop does
/// not implement. See the design spec's section 5.
typedef struct BK_GfxDrawCmd {
  BK_GfxPipeline *pipeline;
  BK_GfxBuffer *vertex_buffer;
  BK_GfxBuffer *index_buffer;
  BK_GfxTexture *texture;
  BK_GfxSampler *sampler;
  BK_GfxBuffer *vertex_storage[BK_GFX_MAX_STORAGE_BUFFERS];
  i32 num_vertex_storage; // highest bound slot + 1, so the bind is one contiguous run
  BK_GfxBuffer *fragment_storage[BK_GFX_MAX_STORAGE_BUFFERS];
  i32 num_fragment_storage;
  const void *vertex_uniform; // arena copy, never the caller's pointer; nullptr if unset
  u32 vertex_uniform_size;
  const void *fragment_uniform;
  u32 fragment_uniform_size;
  BK_Rect scissor;  // width/height <= 0 => full target
  BK_Rect viewport; // width/height <= 0 => full target
  i32 vertex_count;
  i32 index_count;
  i32 instance_count;
  struct BK_GfxDrawCmd *next;
} BK_GfxDrawCmd;

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

/// Test-only accessor: number of draws recorded this frame, reset by the flush.
i32 bk__gfx_get_draw_count(void);

/// Test-only accessor: the index'th recorded draw this frame, in call order, or
/// nullptr if index is out of range.
const BK_GfxDrawCmd *bk__gfx_get_draw_cmd(i32 index);

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

/// Test-only accessor: returns the path set via bk_gfx_request_capture this frame, or
/// an empty string if none has been requested since the last flush.
const char *bk__gfx_get_pending_capture_path(void);

/// Test-only accessor: returns the canvas bound via bk_gfx_bind_canvas this frame, or
/// nullptr if none has been bound since the last flush.
BK_GfxCanvas *bk__gfx_get_pending_canvas(void);

/// Enables or disables the framework-owned swapchain depth-stencil texture (mirrors
/// BK_AppDesc.window.depth_stencil). Called once by bk__boot after the GPU device is
/// created. When enabled, bk__gfx_flush creates/recreates this texture to match the
/// drawable size on every frame that doesn't have a canvas bound.
void bk__gfx_configure_swapchain_depth(bool enabled);

/// Test-only accessor: writes the framework-owned swapchain depth texture's current
/// size to *out_width/*out_height (both 0 if disabled or not created yet -- it's
/// created lazily, on the first flush after boot).
void bk__gfx_get_swapchain_depth_size(i32 *out_width, i32 *out_height);

/// Releases the framework-owned swapchain depth texture, if one was created. Called
/// once by bk__shutdown, before SDL_DestroyGPUDevice.
void bk__gfx_shutdown(void);

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
