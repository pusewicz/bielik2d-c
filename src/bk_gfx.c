#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>

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

void bk__gfx_flush(void) {
    static bool s_logged_acquire_failure = false;

    BK_GfxPipeline *pending_pipeline = s_pending_pipeline;
    int pending_vertex_count = s_pending_vertex_count;
    s_pending_pipeline = nullptr;
    s_pending_vertex_count = 0;

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
    if (!cmd) {
        if (!s_logged_acquire_failure) {
            SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            s_logged_acquire_failure = true;
        }
        return;
    }

    SDL_GPUTexture *tex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, nullptr, nullptr)) {
        SDL_Log("BK: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    if (!tex) {
        // minimized/occluded — nothing to draw into this frame
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    BK_Color c = bk__gfx_get_clear_color();
    SDL_GPUColorTargetInfo target = {
        .texture = tex,
        .clear_color = {c.r, c.g, c.b, c.a},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pending_pipeline));
        SDL_DrawGPUPrimitives(pass, (Uint32)pending_vertex_count, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
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
