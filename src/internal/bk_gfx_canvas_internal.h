#pragma once
#include <bielik/bk_gfx_canvas.h>

#include <SDL3/SDL_gpu.h>

/// Returns the underlying SDL_GPU depth-stencil texture handle, or nullptr if the
/// canvas has no depth-stencil attachment. Framework-internal; used by bk_gfx's frame
/// flush to target the render pass, and by golden-image tests that build a render
/// pass directly against a canvas's attachments.
SDL_GPUTexture *bk__gfx_canvas_depth_handle(const BK_GfxCanvas *canvas);

/// Returns the depth-stencil attachment's format, or SDL_GPU_TEXTUREFORMAT_INVALID if
/// the canvas has none. Framework-internal; used to assert a bound pipeline's declared
/// depth_stencil_format matches the render pass it's drawn into.
SDL_GPUTextureFormat bk__gfx_canvas_depth_format(const BK_GfxCanvas *canvas);

/// Returns the filter mode set via BK_GfxCanvasDesc.blit_filter. Framework-internal;
/// used by bk_gfx's frame flush when blitting the canvas onto the swapchain.
SDL_GPUFilter bk__gfx_canvas_blit_filter(const BK_GfxCanvas *canvas);
