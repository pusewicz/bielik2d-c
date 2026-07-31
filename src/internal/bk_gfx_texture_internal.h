#pragma once
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL_gpu.h>

/// Returns the underlying SDL_GPU texture handle. Framework-internal; used by
/// bk_gfx's frame flush to bind the texture pending from bk_gfx_bind_texture (added
/// in a later task), and by compute dispatch to bind read-write storage textures.
SDL_GPUTexture *bk__gfx_texture_handle(const BK_GfxTexture *texture);

/// Returns the underlying SDL_GPU sampler handle. Framework-internal; used by
/// bk_gfx's frame flush.
SDL_GPUSampler *bk__gfx_sampler_handle(const BK_GfxSampler *sampler);

/// Returns the texture's SDL_GPU format -- R8G8B8A8_UNORM for every usage except
/// BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL. Framework-internal; used by bk_gfx_canvas and
/// bk_gfx_pipeline to match a depth attachment's format against a pipeline's declared
/// depth_stencil_format.
SDL_GPUTextureFormat bk__gfx_texture_format(const BK_GfxTexture *texture);
