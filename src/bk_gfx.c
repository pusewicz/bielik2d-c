#include "internal/bk_alloc_internal.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_canvas_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_canvas.h>

#include <SDL3/SDL.h>

static BK_Color s_clear_color = {0.1f, 0.1f, 0.12f, 1.0f};

void bk_gfx_set_clear_color(BK_Color color) {
  s_clear_color = color;
}

BK_Color bk__gfx_get_clear_color(void) {
  return s_clear_color;
}

// Currently bound state, plus this frame's chain of recorded draws. s_state is the same
// type as a record, so appending a draw is one struct copy -- a record snapshots the
// state at call time rather than referencing it. Without that, every draw in a frame
// would replay with the frame's *final* state instead of its own.
static BK_GfxDrawCmd s_state;
static BK_GfxDrawCmd *s_draw_head = nullptr;
static BK_GfxDrawCmd *s_draw_tail = nullptr;
static i32 s_draw_count = 0;

static void s_record_draw(i32 vertex_count, i32 index_count, i32 instance_count) {
  BK_GfxDrawCmd *cmd = bk_frame_alloc(sizeof *cmd, alignof(BK_GfxDrawCmd));
  if (cmd == nullptr) {
    // bk_frame_alloc's growth allocation aborts on real OOM now (DEVIATIONS.md), so this
    // is defense-in-depth, not a reachable path today -- kept because dropping the draw
    // beats dereferencing null if that policy ever changes, and the frame still clears
    // and presents.
    return;
  }
  *cmd = s_state;
  cmd->vertex_count = vertex_count;
  cmd->index_count = index_count;
  cmd->instance_count = instance_count;
  cmd->next = nullptr;
  if (s_draw_tail == nullptr) {
    s_draw_head = cmd;
  } else {
    s_draw_tail->next = cmd;
  }
  s_draw_tail = cmd;
  s_draw_count++;
}

void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline) {
  BK_ASSERT(pipeline != nullptr);
  s_state.pipeline = pipeline;
}

void bk_gfx_draw(i32 vertex_count) {
  BK_ASSERT(vertex_count > 0);
  s_record_draw(vertex_count, 0, 1);
}

BK_GfxPipeline *bk__gfx_get_pending_pipeline(void) {
  return s_state.pipeline;
}

i32 bk__gfx_get_draw_count(void) {
  return s_draw_count;
}

const BK_GfxDrawCmd *bk__gfx_get_draw_cmd(i32 index) {
  if (index < 0) {
    return nullptr;
  }
  const BK_GfxDrawCmd *cmd = s_draw_head;
  for (i32 i = 0; i < index && cmd != nullptr; ++i) {
    cmd = cmd->next;
  }
  return cmd;
}

void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer) {
  BK_ASSERT(buffer != nullptr);
  s_state.vertex_buffer = buffer;
}

void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer) {
  BK_ASSERT(buffer != nullptr);
  s_state.index_buffer = buffer;
}

void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler) {
  BK_ASSERT(texture != nullptr);
  BK_ASSERT(sampler != nullptr);
  s_state.texture = texture;
  s_state.sampler = sampler;
}

void bk_gfx_draw_indexed(i32 index_count) {
  BK_ASSERT(index_count > 0);
  s_record_draw(0, index_count, 1);
}

BK_GfxBuffer *bk__gfx_get_pending_vertex_buffer(void) {
  return s_state.vertex_buffer;
}

BK_GfxBuffer *bk__gfx_get_pending_index_buffer(void) {
  return s_state.index_buffer;
}

BK_GfxTexture *bk__gfx_get_pending_texture(void) {
  return s_state.texture;
}

BK_GfxSampler *bk__gfx_get_pending_sampler(void) {
  return s_state.sampler;
}

void bk_gfx_bind_vertex_storage_buffer(BK_GfxBuffer *buffer, i32 slot) {
  BK_ASSERT(buffer != nullptr);
  BK_ASSERT(slot >= 0 && slot < BK_GFX_MAX_STORAGE_BUFFERS);
  s_state.vertex_storage[slot] = buffer;
  if (slot + 1 > s_state.num_vertex_storage) {
    s_state.num_vertex_storage = slot + 1;
  }
}

void bk_gfx_bind_fragment_storage_buffer(BK_GfxBuffer *buffer, i32 slot) {
  BK_ASSERT(buffer != nullptr);
  BK_ASSERT(slot >= 0 && slot < BK_GFX_MAX_STORAGE_BUFFERS);
  s_state.fragment_storage[slot] = buffer;
  if (slot + 1 > s_state.num_fragment_storage) {
    s_state.num_fragment_storage = slot + 1;
  }
}

// The arena copy is what makes passing a stack local safe: SDL_PushGPU*UniformData
// doesn't run until flush, long after the caller's frame is gone.
static const void *s_copy_uniform(const void *data, u32 size) {
  BK_ASSERT(data != nullptr);
  BK_ASSERT(size > 0);
  void *copy = bk_frame_alloc(size, 0); // 0 => platform max alignment, per its contract
  if (copy == nullptr) {
    return nullptr; // defense-in-depth -- bk_frame_alloc's growth now aborts on real OOM
                    // (DEVIATIONS.md), so this path isn't reachable today
  }
  SDL_memcpy(copy, data, size);
  return copy;
}

void bk_gfx_push_vertex_uniform(const void *data, u32 size) {
  const void *copy = s_copy_uniform(data, size);
  if (copy == nullptr) {
    return;
  }
  s_state.vertex_uniform = copy;
  s_state.vertex_uniform_size = size;
}

void bk_gfx_push_fragment_uniform(const void *data, u32 size) {
  const void *copy = s_copy_uniform(data, size);
  if (copy == nullptr) {
    return;
  }
  s_state.fragment_uniform = copy;
  s_state.fragment_uniform_size = size;
}

void bk_gfx_draw_instanced(i32 vertex_count, i32 instance_count) {
  BK_ASSERT(vertex_count > 0);
  BK_ASSERT(instance_count > 0);
  s_record_draw(vertex_count, 0, instance_count);
}

void bk_gfx_set_scissor(BK_Rect rect) {
  s_state.scissor = rect;
}

void bk_gfx_set_viewport(BK_Rect rect) {
  s_state.viewport = rect;
}

// Frame-level, not a record field: see BK_GfxDrawCmd's comment and spec section 5.
static BK_GfxCanvas *s_pending_canvas = nullptr;

void bk_gfx_bind_canvas(BK_GfxCanvas *canvas) {
  s_pending_canvas = canvas;
}

BK_GfxCanvas *bk__gfx_get_pending_canvas(void) {
  return s_pending_canvas;
}

static bool s_swapchain_depth_enabled = false;

SDL_GPUTextureFormat bk__gfx_pending_target_depth_format(void) {
  if (s_pending_canvas != nullptr) {
    return bk__gfx_canvas_depth_format(s_pending_canvas);
  }
  return s_swapchain_depth_enabled ? bk_gfx_depth_stencil_format(bk_gpu())
                                   : SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUTextureFormat bk__gfx_pending_target_color_format(void) {
  if (s_pending_canvas != nullptr) {
    return bk__gfx_texture_format(bk_gfx_canvas_texture(s_pending_canvas));
  }
  return SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
}

static BK_GfxTexture *s_swapchain_depth_texture = nullptr;
static i32 s_swapchain_depth_w = 0;
static i32 s_swapchain_depth_h = 0;

void bk__gfx_configure_swapchain_depth(bool enabled) {
  s_swapchain_depth_enabled = enabled;
}

void bk__gfx_get_swapchain_depth_size(i32 *out_width, i32 *out_height) {
  if (out_width != nullptr) {
    *out_width = s_swapchain_depth_w;
  }
  if (out_height != nullptr) {
    *out_height = s_swapchain_depth_h;
  }
}

static char s_pending_capture_path[512];

void bk__gfx_shutdown(void) {
  bk_gfx_texture_destroy(s_swapchain_depth_texture);
  s_swapchain_depth_texture = nullptr;
  s_swapchain_depth_w = 0;
  s_swapchain_depth_h = 0;

  // Not redundant with bk__gfx_flush's own snapshot-and-clear block at its top: bk__iterate
  // returns early when update or post_update returns non-BK_CONTINUE, skipping
  // bk__draw_collate/bk__gfx_flush entirely. Without this, an app that binds a pipeline,
  // canvas, or capture path -- or records a draw -- from update and quits on the same tick
  // leaves these pointing into memory the app's own quit callback (or bk__arena_free, for
  // the draw chain) is about to release, for a second bk_run in the same process to walk.
  // s_pending_canvas is the most directly reachable of these: bk__draw_collate reads it
  // unconditionally on every bk__iterate call, including the first, via
  // bk__gfx_get_pending_canvas.
  s_state = (BK_GfxDrawCmd){0};
  s_draw_head = nullptr;
  s_draw_tail = nullptr;
  s_draw_count = 0;
  s_pending_canvas = nullptr;
  s_pending_capture_path[0] = '\0';
}

void bk_gfx_request_capture(const char *path) {
  BK_ASSERT(path != nullptr);
  SDL_snprintf(s_pending_capture_path, sizeof s_pending_capture_path, "%s", path);
}

const char *bk__gfx_get_pending_capture_path(void) {
  return s_pending_capture_path;
}

static SDL_PixelFormat s_pixel_format_for_gpu_format(SDL_GPUTextureFormat format) {
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    return SDL_PIXELFORMAT_RGBA32;
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
    return SDL_PIXELFORMAT_BGRA32;
  default:
    return SDL_PIXELFORMAT_UNKNOWN;
  }
}

void bk__gfx_flush(void) {
  static bool s_logged_acquire_failure = false;

  // Snapshot and clear everything up front, before any early return below. This is
  // load-bearing, not stylistic: bk__iterate calls bk__gfx_flush() then
  // bk__arena_reset() unconditionally, and this function returns early on a failed
  // command-buffer acquire and on a null swapchain texture (minimized/occluded window).
  // Clearing at the end would leave the draw chain pointing into arena memory that
  // bk__arena_reset then hands out again, so the next frame's records would overwrite
  // this frame's chain while it was still linked.
  BK_GfxDrawCmd *draw_head = s_draw_head;
  BK_GfxCanvas *pending_canvas = s_pending_canvas;
  char pending_capture_path[sizeof s_pending_capture_path];
  SDL_memcpy(pending_capture_path, s_pending_capture_path, sizeof pending_capture_path);
  s_state = (BK_GfxDrawCmd){0};
  s_draw_head = nullptr;
  s_draw_tail = nullptr;
  s_draw_count = 0;
  s_pending_canvas = nullptr;
  s_pending_capture_path[0] = '\0';

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
  if (!cmd) {
    if (!s_logged_acquire_failure) {
      SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
      s_logged_acquire_failure = true;
    }
    return;
  }

  Uint32 swap_w = 0, swap_h = 0;
  SDL_GPUTexture *tex = nullptr;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, &swap_w, &swap_h)) {
    SDL_Log("BK: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }
  if (!tex) {
    // minimized/occluded — nothing to draw into this frame
    if (pending_capture_path[0] != '\0') {
      SDL_Log("BK: bk_gfx_request_capture: dropped, window minimized/occluded this frame");
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }

  // Render into the bound canvas (if any) instead of the swapchain, plus its depth
  // attachment (if it has one). With no canvas bound, the swapchain gets the
  // framework-owned depth texture instead, if BK_WindowDesc.depth_stencil requested
  // one (bk__gfx_configure_swapchain_depth, called once from bk__boot).
  SDL_GPUTexture *color_target_texture = tex;
  Uint32 target_w = swap_w;
  Uint32 target_h = swap_h;
  SDL_GPUTexture *depth_target_texture = nullptr;
  SDL_GPUTextureFormat depth_target_format = SDL_GPU_TEXTUREFORMAT_INVALID;
  // The canvas branch below reads the real attachment's format straight off the
  // texture via bk__gfx_texture_format. The swapchain branch can't do that: an
  // acquired SDL_GPUTexture* has no format accessor of its own, so this has to
  // re-query SDL_GetGPUSwapchainTextureFormat -- the same query the capture path
  // further down in this function already relies on for the same reason.
  SDL_GPUTextureFormat color_target_format =
      SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
  if (pending_canvas != nullptr) {
    color_target_texture = bk__gfx_texture_handle(bk_gfx_canvas_texture(pending_canvas));
    i32 canvas_w = 0, canvas_h = 0;
    bk_gfx_canvas_size(pending_canvas, &canvas_w, &canvas_h);
    target_w = (Uint32)canvas_w;
    target_h = (Uint32)canvas_h;
    depth_target_texture = bk__gfx_canvas_depth_handle(pending_canvas);
    depth_target_format = bk__gfx_canvas_depth_format(pending_canvas);
    color_target_format = bk__gfx_texture_format(bk_gfx_canvas_texture(pending_canvas));
  } else if (s_swapchain_depth_enabled) {
    if (s_swapchain_depth_texture == nullptr || s_swapchain_depth_w != (i32)swap_w ||
        s_swapchain_depth_h != (i32)swap_h) {
      // SDL_GPU defers actual destruction past any command buffer still
      // referencing the old texture, so no fence wait is needed before
      // recreating at the new size -- same reasoning as a canvas destroy.
      bk_gfx_texture_destroy(s_swapchain_depth_texture);
      s_swapchain_depth_texture = bk_gfx_texture_create(
          bk_gpu(), BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL, (i32)swap_w, (i32)swap_h);
      s_swapchain_depth_w = s_swapchain_depth_texture != nullptr ? (i32)swap_w : 0;
      s_swapchain_depth_h = s_swapchain_depth_texture != nullptr ? (i32)swap_h : 0;
    }
    if (s_swapchain_depth_texture != nullptr) {
      depth_target_texture = bk__gfx_texture_handle(s_swapchain_depth_texture);
      depth_target_format = bk__gfx_texture_format(s_swapchain_depth_texture);
    }
  }

  BK_Color clear_color = bk__gfx_get_clear_color();
  SDL_GPUColorTargetInfo target = {
      .texture = color_target_texture,
      .clear_color = {clear_color.r, clear_color.g, clear_color.b, clear_color.a},
      .load_op = SDL_GPU_LOADOP_CLEAR,
      .store_op = SDL_GPU_STOREOP_STORE,
  };
  SDL_GPUDepthStencilTargetInfo depth_target = {0};
  SDL_GPUDepthStencilTargetInfo *depth_target_ptr = nullptr;
  if (depth_target_texture != nullptr) {
    depth_target = (SDL_GPUDepthStencilTargetInfo){
        .texture = depth_target_texture,
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_CLEAR,
        .stencil_store_op = SDL_GPU_STOREOP_STORE,
        .cycle = true,
    };
    depth_target_ptr = &depth_target;
  }
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, depth_target_ptr);

  // Replay the frame's draws in record order. Only the pipeline bind is elided when
  // unchanged between consecutive records; buffers and textures rebind each time.
  // Eliding those too is a measurable-win optimization with no consumer yet, and
  // correctness first keeps the P3.3 batch's behavior easy to reason about.
  const BK_GfxPipeline *bound_pipeline = nullptr;
  for (const BK_GfxDrawCmd *draw = draw_head; draw != nullptr; draw = draw->next) {
    if (draw->pipeline == nullptr) {
      continue; // a draw recorded with no pipeline bound has nothing to draw with
    }
    if (draw->pipeline != bound_pipeline) {
      // Per record, not once per frame: a frame mixing a depth-enabled and a
      // depth-disabled pipeline must keep this named diagnostic for both. Catches a
      // depth-attachment/pipeline mismatch here instead of as SDL_GPU's opaque
      // "pipeline incompatible with render pass" validation failure.
      BK_ASSERT(bk__gfx_pipeline_depth_format(draw->pipeline) == depth_target_format);
      BK_ASSERT(bk__gfx_pipeline_color_format(draw->pipeline) == color_target_format);
      SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(draw->pipeline));
      bound_pipeline = draw->pipeline;
    }
    if (draw->vertex_buffer != nullptr) {
      SDL_GPUBufferBinding vertex_binding = {.buffer = bk__gfx_buffer_handle(draw->vertex_buffer)};
      SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    }
    if (draw->index_buffer != nullptr) {
      SDL_GPUBufferBinding index_binding = {.buffer = bk__gfx_buffer_handle(draw->index_buffer)};
      SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    }
    if (draw->texture != nullptr && draw->sampler != nullptr) {
      SDL_GPUTextureSamplerBinding sampler_binding = {
          .texture = bk__gfx_texture_handle(draw->texture),
          .sampler = bk__gfx_sampler_handle(draw->sampler)};
      SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);
    }
    if (draw->num_vertex_storage > 0) {
      SDL_GPUBuffer *handles[BK_GFX_MAX_STORAGE_BUFFERS] = {nullptr};
      for (i32 i = 0; i < draw->num_vertex_storage; ++i) {
        handles[i] = draw->vertex_storage[i] != nullptr
                         ? bk__gfx_buffer_handle(draw->vertex_storage[i])
                         : nullptr;
      }
      SDL_BindGPUVertexStorageBuffers(pass, 0, handles, (Uint32)draw->num_vertex_storage);
    }
    if (draw->num_fragment_storage > 0) {
      SDL_GPUBuffer *handles[BK_GFX_MAX_STORAGE_BUFFERS] = {nullptr};
      for (i32 i = 0; i < draw->num_fragment_storage; ++i) {
        handles[i] = draw->fragment_storage[i] != nullptr
                         ? bk__gfx_buffer_handle(draw->fragment_storage[i])
                         : nullptr;
      }
      SDL_BindGPUFragmentStorageBuffers(pass, 0, handles, (Uint32)draw->num_fragment_storage);
    }
    // Uniforms push to the command buffer, not the render pass -- that's SDL's API
    // shape; the data applies to subsequent draws recorded on that command buffer.
    if (draw->vertex_uniform != nullptr) {
      SDL_PushGPUVertexUniformData(cmd, 0, draw->vertex_uniform, draw->vertex_uniform_size);
    }
    if (draw->fragment_uniform != nullptr) {
      SDL_PushGPUFragmentUniformData(cmd, 0, draw->fragment_uniform, draw->fragment_uniform_size);
    }
    // Set explicitly on every record rather than only when non-default: SDL has no
    // "reset scissor" call, so skipping would leak the previous record's rect into a
    // record that never set one.
    SDL_Rect scissor = {0, 0, (int)target_w, (int)target_h};
    if (draw->scissor.width > 0 && draw->scissor.height > 0) {
      scissor =
          (SDL_Rect){draw->scissor.x, draw->scissor.y, draw->scissor.width, draw->scissor.height};
    }
    SDL_SetGPUScissor(pass, &scissor);

    SDL_GPUViewport viewport = {0.0f, 0.0f, (float)target_w, (float)target_h, 0.0f, 1.0f};
    if (draw->viewport.width > 0 && draw->viewport.height > 0) {
      viewport = (SDL_GPUViewport){(float)draw->viewport.x,
                                   (float)draw->viewport.y,
                                   (float)draw->viewport.width,
                                   (float)draw->viewport.height,
                                   0.0f,
                                   1.0f};
    }
    SDL_SetGPUViewport(pass, &viewport);

    if (draw->index_count > 0) {
      SDL_DrawGPUIndexedPrimitives(pass, (Uint32)draw->index_count, (Uint32)draw->instance_count, 0,
                                   0, 0);
    } else if (draw->vertex_count > 0) {
      SDL_DrawGPUPrimitives(pass, (Uint32)draw->vertex_count, (Uint32)draw->instance_count, 0, 0);
    }
  }
  SDL_EndGPURenderPass(pass);

  if (pending_canvas != nullptr) {
    // Stretch the canvas onto the swapchain -- a canvas smaller (or larger) than
    // the window is the fixed-internal-resolution / pixel-art path. Must run after
    // SDL_EndGPURenderPass: a texture can't be blit from/to while it's bound as an
    // active render pass target.
    SDL_GPUBlitInfo blit_info = {
        .source = {.texture = color_target_texture, .w = target_w, .h = target_h},
        .destination = {.texture = tex, .w = swap_w, .h = swap_h},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .clear_color = {clear_color.r, clear_color.g, clear_color.b, clear_color.a},
        .flip_mode = SDL_FLIP_NONE,
        .filter = bk__gfx_canvas_blit_filter(pending_canvas),
        .cycle = true,
    };
    SDL_BlitGPUTexture(cmd, &blit_info);
  }

  if (pending_capture_path[0] != '\0') {
    SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
    SDL_PixelFormat sdl_format = s_pixel_format_for_gpu_format(format);
    if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
      SDL_Log("BK: bk_gfx_request_capture: unsupported swapchain format");
      SDL_SubmitGPUCommandBuffer(cmd);
    } else {
      void *pixels = bk__gfx_download_texture(bk_gpu(), cmd, tex, swap_w, swap_h, format);
      if (pixels != nullptr) {
        SDL_Surface *surface =
            SDL_CreateSurfaceFrom((int)swap_w, (int)swap_h, sdl_format, pixels, (int)swap_w * 4);
        if (surface != nullptr) {
          if (!SDL_SaveBMP(surface, pending_capture_path)) {
            SDL_Log("BK: SDL_SaveBMP failed: %s", SDL_GetError());
          }
          SDL_DestroySurface(surface);
        } else {
          SDL_Log("BK: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
        }
        BK__FREE(nullptr, BK_MEM_TAG_GFX, pixels, (isize)swap_w * (isize)swap_h * 4);
      }
    }
  } else {
    SDL_SubmitGPUCommandBuffer(cmd);
  }
}

void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format) {
  BK_ASSERT(device != nullptr);
  BK_ASSERT(cmd != nullptr);
  BK_ASSERT(texture != nullptr);
  BK_ASSERT(format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
            format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);

  SDL_GPUTransferBufferCreateInfo transfer_info = {
      .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
      .size = width * height * 4,
  };
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) {
    SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_SubmitGPUCommandBuffer(cmd);
    return nullptr;
  }

  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion src = {.texture = texture, .w = width, .h = height, .d = 1};
  SDL_GPUTextureTransferInfo dst = {
      .transfer_buffer = transfer, .pixels_per_row = width, .rows_per_layer = height};
  SDL_DownloadFromGPUTexture(copy_pass, &src, &dst);
  SDL_EndGPUCopyPass(copy_pass);

  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence == nullptr) {
    SDL_Log("BK: SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return nullptr;
  }
  if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
    SDL_Log("BK: SDL_WaitForGPUFences failed: %s", SDL_GetError());
    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return nullptr;
  }
  SDL_ReleaseGPUFence(device, fence);

  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (mapped == nullptr) {
    SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return nullptr;
  }

  usize byte_size = (usize)width * (usize)height * 4;
  void *pixels = BK__ALLOC(nullptr, BK_MEM_TAG_GFX, (isize)byte_size);
  SDL_memcpy(pixels, mapped, byte_size);

  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return pixels;
}
