#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_types.h>

/// What a texture is used for.
typedef enum BK_GfxTextureUsage {
    BK_GFX_TEXTURE_USAGE_SAMPLER,        // CPU-uploaded, sampled by a fragment shader
    BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, // written by a compute shader, then sampled
} BK_GfxTextureUsage;

typedef enum BK_GfxFilter {
    BK_GFX_FILTER_NEAREST,
    BK_GFX_FILTER_LINEAR,
} BK_GfxFilter;

typedef enum BK_GfxAddressMode {
    BK_GFX_ADDRESS_CLAMP,
    BK_GFX_ADDRESS_REPEAT,
} BK_GfxAddressMode;

/// An R8G8B8A8_UNORM 2D texture -- the only format this module supports (a
/// sprite/atlas path). More formats get added when a real use case demands them.
typedef struct BK_GfxTexture BK_GfxTexture;

/// A sampler: filtering + addressing state, bound alongside a texture at draw time.
typedef struct BK_GfxSampler BK_GfxSampler;

/// Creates a width x height R8G8B8A8_UNORM texture for the given usage. Logs via
/// SDL_Log ("BK: " prefix) and returns nullptr on SDL_GPU failure. device is
/// explicit, matching bk_gfx_pipeline_create's precedent.
BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage, i32 width,
                                     i32 height);

/// Uploads width*height RGBA8 pixels (tightly packed, 4 bytes/pixel) into texture.
/// Only valid for a BK_GFX_TEXTURE_USAGE_SAMPLER texture -- BK_ASSERTs otherwise,
/// since uploading into a compute-write target is a programmer error, not a runtime
/// condition. Returns false and logs via SDL_Log on SDL_GPU failure.
bool bk_gfx_texture_upload(BK_GfxTexture *texture, const void *rgba_pixels);

/// Destroys a texture. No-op if texture is nullptr.
void bk_gfx_texture_destroy(BK_GfxTexture *texture);

/// Creates a sampler with the given filter and addressing mode (applied to both U and
/// V). Logs via SDL_Log and returns nullptr on SDL_GPU failure.
BK_GfxSampler *bk_gfx_sampler_create(SDL_GPUDevice *device, BK_GfxFilter filter,
                                     BK_GfxAddressMode address_mode);

/// Destroys a sampler. No-op if sampler is nullptr.
void bk_gfx_sampler_destroy(BK_GfxSampler *sampler);
