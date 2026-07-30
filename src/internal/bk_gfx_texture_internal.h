#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_texture.h>

/// Returns the underlying SDL_GPU texture handle. Framework-internal; used by
/// bk_gfx's frame flush to bind the texture pending from bk_gfx_bind_texture (added
/// in a later task), and by compute dispatch to bind read-write storage textures.
SDL_GPUTexture *bk__gfx_texture_handle(const BK_GfxTexture *texture);

/// Returns the underlying SDL_GPU sampler handle. Framework-internal; used by
/// bk_gfx's frame flush.
SDL_GPUSampler *bk__gfx_sampler_handle(const BK_GfxSampler *sampler);
