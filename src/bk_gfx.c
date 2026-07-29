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
    if (s_pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(s_pending_pipeline));
        SDL_DrawGPUPrimitives(pass, (Uint32)s_pending_vertex_count, 1, 0, 0);
        s_pending_pipeline = nullptr;
        s_pending_vertex_count = 0;
    }
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
}
