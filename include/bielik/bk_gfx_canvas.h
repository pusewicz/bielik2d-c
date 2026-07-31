#pragma once
#include <SDL3/SDL_gpu.h>

/// Returns the best depth-stencil format this device supports, probed in preference
/// order D24_UNORM_S8_UINT -> D32_FLOAT_S8_UINT -> D16_UNORM. SDL_gpu.h's
/// DEPTH_STENCIL_TARGET usage docs guarantee D16_UNORM plus one (not necessarily
/// both) of the two S8 formats on every backend, so this always returns something
/// usable -- but D16_UNORM alone has no stencil bits, and Metal has no D24S8, so the
/// D32S8 fallback matters: skipping it would silently drop stencil support on macOS.
/// Pipelines drawing into a depth-enabled pass must pass this value as
/// BK_GfxPipelineDesc.depth_stencil_format so the pipeline's declared attachment
/// format matches what the depth texture was actually created with.
SDL_GPUTextureFormat bk_gfx_depth_stencil_format(SDL_GPUDevice *device);
