#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_texture.h>

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

/// An offscreen render target: a color texture (and optional depth-stencil texture)
/// that the frame's render pass can target instead of the swapchain, via
/// bk_gfx_bind_canvas. The color attachment is sampleable, so a canvas can also be
/// drawn as an ordinary textured quad once rendered.
typedef struct BK_GfxCanvas BK_GfxCanvas;

typedef struct BK_GfxCanvasDesc {
    int width, height;
    bool depth_stencil;       // allocate a depth-stencil attachment
    BK_GfxFilter blit_filter; // filter used when blitting to the swapchain (0 == NEAREST)
} BK_GfxCanvasDesc;

/// Creates a canvas: an R8G8B8A8_UNORM color texture sized width x height, plus (if
/// depth_stencil) a depth-stencil texture at bk_gfx_depth_stencil_format(device).
/// Logs via SDL_Log ("BK: " prefix) and returns nullptr on non-positive dimensions or
/// SDL_GPU failure.
BK_GfxCanvas *bk_gfx_canvas_create(SDL_GPUDevice *device, const BK_GfxCanvasDesc *desc);

/// Destroys a canvas and its attachment texture(s). No-op if canvas is nullptr.
void bk_gfx_canvas_destroy(BK_GfxCanvas *canvas);

/// The canvas's color attachment -- an ordinary BK_GfxTexture, bindable via
/// bk_gfx_bind_texture like any other sampled texture.
BK_GfxTexture *bk_gfx_canvas_texture(BK_GfxCanvas *canvas);

/// Writes the canvas's width/height in pixels to *out_width/*out_height.
void bk_gfx_canvas_size(const BK_GfxCanvas *canvas, int *out_width, int *out_height);
