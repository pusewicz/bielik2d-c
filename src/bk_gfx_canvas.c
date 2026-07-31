#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx_canvas.h>

SDL_GPUTextureFormat bk_gfx_depth_stencil_format(SDL_GPUDevice *device) {
    BK_ASSERT(device != nullptr);

    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                     SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                     SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}
