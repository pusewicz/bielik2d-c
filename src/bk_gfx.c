#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_canvas_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_canvas.h>

static BK_Color s_clear_color = {0.1f, 0.1f, 0.12f, 1.0f};

void bk_gfx_set_clear_color(BK_Color color) { s_clear_color = color; }

BK_Color bk__gfx_get_clear_color(void) { return s_clear_color; }

static BK_GfxPipeline *s_pending_pipeline = nullptr;
static int s_pending_vertex_count = 0;

void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline) {
    BK_ASSERT(pipeline != nullptr);
    s_pending_pipeline = pipeline;
}

void bk_gfx_draw(int vertex_count) {
    BK_ASSERT(vertex_count > 0);
    s_pending_vertex_count = vertex_count;
}

BK_GfxPipeline *bk__gfx_get_pending_pipeline(void) { return s_pending_pipeline; }

int bk__gfx_get_pending_vertex_count(void) { return s_pending_vertex_count; }

static BK_GfxBuffer *s_pending_vertex_buffer = nullptr;
static BK_GfxBuffer *s_pending_index_buffer = nullptr;
static BK_GfxTexture *s_pending_texture = nullptr;
static BK_GfxSampler *s_pending_sampler = nullptr;
static int s_pending_index_count = 0;

void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    s_pending_vertex_buffer = buffer;
}

void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    s_pending_index_buffer = buffer;
}

void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler) {
    BK_ASSERT(texture != nullptr);
    BK_ASSERT(sampler != nullptr);
    s_pending_texture = texture;
    s_pending_sampler = sampler;
}

void bk_gfx_draw_indexed(int index_count) {
    BK_ASSERT(index_count > 0);
    s_pending_index_count = index_count;
}

BK_GfxBuffer *bk__gfx_get_pending_vertex_buffer(void) { return s_pending_vertex_buffer; }

BK_GfxBuffer *bk__gfx_get_pending_index_buffer(void) { return s_pending_index_buffer; }

BK_GfxTexture *bk__gfx_get_pending_texture(void) { return s_pending_texture; }

BK_GfxSampler *bk__gfx_get_pending_sampler(void) { return s_pending_sampler; }

int bk__gfx_get_pending_index_count(void) { return s_pending_index_count; }

static BK_GfxCanvas *s_pending_canvas = nullptr;

void bk_gfx_bind_canvas(BK_GfxCanvas *canvas) { s_pending_canvas = canvas; }

BK_GfxCanvas *bk__gfx_get_pending_canvas(void) { return s_pending_canvas; }

static char s_pending_capture_path[512];

void bk_gfx_request_capture(const char *path) {
    BK_ASSERT(path != nullptr);
    SDL_snprintf(s_pending_capture_path, sizeof s_pending_capture_path, "%s", path);
}

const char *bk__gfx_get_pending_capture_path(void) { return s_pending_capture_path; }

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

    BK_GfxPipeline *pending_pipeline = s_pending_pipeline;
    int pending_vertex_count = s_pending_vertex_count;
    BK_GfxBuffer *pending_vertex_buffer = s_pending_vertex_buffer;
    BK_GfxBuffer *pending_index_buffer = s_pending_index_buffer;
    BK_GfxTexture *pending_texture = s_pending_texture;
    BK_GfxSampler *pending_sampler = s_pending_sampler;
    int pending_index_count = s_pending_index_count;
    BK_GfxCanvas *pending_canvas = s_pending_canvas;
    char pending_capture_path[sizeof s_pending_capture_path];
    SDL_memcpy(pending_capture_path, s_pending_capture_path, sizeof pending_capture_path);
    s_pending_pipeline = nullptr;
    s_pending_vertex_count = 0;
    s_pending_vertex_buffer = nullptr;
    s_pending_index_buffer = nullptr;
    s_pending_texture = nullptr;
    s_pending_sampler = nullptr;
    s_pending_index_count = 0;
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
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // Render into the bound canvas (if any) instead of the swapchain, plus its depth
    // attachment (if it has one). Rendering directly into the swapchain has no
    // framework-owned depth attachment yet -- BK_WindowDesc.depth_stencil (added in a
    // later task) is what makes depth_target_texture non-null on that path too.
    SDL_GPUTexture *color_target_texture = tex;
    Uint32 target_w = swap_w;
    Uint32 target_h = swap_h;
    SDL_GPUTexture *depth_target_texture = nullptr;
    SDL_GPUTextureFormat depth_target_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (pending_canvas != nullptr) {
        color_target_texture = bk__gfx_texture_handle(bk_gfx_canvas_texture(pending_canvas));
        int canvas_w = 0, canvas_h = 0;
        bk_gfx_canvas_size(pending_canvas, &canvas_w, &canvas_h);
        target_w = (Uint32)canvas_w;
        target_h = (Uint32)canvas_h;
        depth_target_texture = bk__gfx_canvas_depth_handle(pending_canvas);
        depth_target_format = bk__gfx_canvas_depth_format(pending_canvas);
    }

    BK_Color c = bk__gfx_get_clear_color();
    SDL_GPUColorTargetInfo target = {
        .texture = color_target_texture,
        .clear_color = {c.r, c.g, c.b, c.a},
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
    if (pending_pipeline != nullptr) {
        // Catches a depth-attachment/pipeline mismatch (canvas has depth but the
        // pipeline was created without it, or vice versa, or the formats differ) as a
        // named assertion here instead of SDL_GPU's opaque "pipeline incompatible with
        // render pass" validation failure.
        BK_ASSERT(bk__gfx_pipeline_depth_format(pending_pipeline) == depth_target_format);
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pending_pipeline));
        if (pending_vertex_buffer != nullptr) {
            SDL_GPUBufferBinding vertex_binding = {
                .buffer = bk__gfx_buffer_handle(pending_vertex_buffer)};
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        }
        if (pending_index_buffer != nullptr) {
            SDL_GPUBufferBinding index_binding = {.buffer =
                                                      bk__gfx_buffer_handle(pending_index_buffer)};
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        }
        if (pending_texture != nullptr && pending_sampler != nullptr) {
            SDL_GPUTextureSamplerBinding sampler_binding = {
                .texture = bk__gfx_texture_handle(pending_texture),
                .sampler = bk__gfx_sampler_handle(pending_sampler)};
            SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);
        }
        if (pending_vertex_count > 0) {
            SDL_DrawGPUPrimitives(pass, (Uint32)pending_vertex_count, 1, 0, 0);
        }
        if (pending_index_count > 0) {
            SDL_DrawGPUIndexedPrimitives(pass, (Uint32)pending_index_count, 1, 0, 0, 0);
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
            .clear_color = {c.r, c.g, c.b, c.a},
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
                SDL_Surface *surface = SDL_CreateSurfaceFrom((int)swap_w, (int)swap_h, sdl_format,
                                                             pixels, (int)swap_w * 4);
                if (surface != nullptr) {
                    if (!SDL_SaveBMP(surface, pending_capture_path)) {
                        SDL_Log("BK: SDL_SaveBMP failed: %s", SDL_GetError());
                    }
                    SDL_DestroySurface(surface);
                } else {
                    SDL_Log("BK: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
                }
                bk__free(pixels);
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

    size_t byte_size = (size_t)width * (size_t)height * 4;
    void *pixels = bk__alloc(byte_size);
    if (pixels != nullptr) {
        SDL_memcpy(pixels, mapped, byte_size);
    }

    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return pixels;
}
