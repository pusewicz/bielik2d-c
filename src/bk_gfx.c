#include "internal/bk_gfx_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>

static BK_Color s_clear_color = {0.1f, 0.1f, 0.12f, 1.0f};

void bk_gfx_set_clear_color(BK_Color color) { s_clear_color = color; }

BK_Color bk__gfx_get_clear_color(void) { return s_clear_color; }

void bk_gfx__flush(void) {
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
    if (!cmd) {
        SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    SDL_GPUTexture *tex = NULL;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, NULL, NULL)) {
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
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
}
