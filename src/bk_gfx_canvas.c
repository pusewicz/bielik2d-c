#include "internal/bk_alloc_internal.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_canvas_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_texture.h>

#include <SDL3/SDL.h>

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

struct BK_GfxCanvas {
  BK_GfxTexture *color;
  BK_GfxTexture *depth; // nullptr if the canvas has no depth-stencil attachment
  SDL_GPUFilter blit_filter;
  i32 width;
  i32 height;
};

BK_GfxCanvas *bk_gfx_canvas_create(SDL_GPUDevice *device, const BK_GfxCanvasDesc *desc) {
  BK_ASSERT(device != nullptr);
  BK_ASSERT(desc != nullptr);

  if (desc->width <= 0 || desc->height <= 0) {
    SDL_Log("BK: bk_gfx_canvas_create: width/height must be > 0, got %dx%d", desc->width,
            desc->height);
    return nullptr;
  }

  BK_GfxTexture *color =
      bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_RENDER_TARGET, desc->width, desc->height);
  if (color == nullptr) {
    return nullptr;
  }

  BK_GfxTexture *depth = nullptr;
  if (desc->depth_stencil) {
    depth = bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL, desc->width,
                                  desc->height);
    if (depth == nullptr) {
      bk_gfx_texture_destroy(color);
      return nullptr;
    }
  }

  BK_GfxCanvas *canvas = BK__ALLOC(nullptr, BK_MEM_TAG_GFX, (isize)sizeof(BK_GfxCanvas));
  canvas->color = color;
  canvas->depth = depth;
  canvas->blit_filter =
      desc->blit_filter == BK_GFX_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
  canvas->width = desc->width;
  canvas->height = desc->height;
  return canvas;
}

void bk_gfx_canvas_destroy(BK_GfxCanvas *canvas) {
  if (canvas == nullptr) {
    return;
  }
  bk_gfx_texture_destroy(canvas->color);
  bk_gfx_texture_destroy(canvas->depth);
  BK__FREE(nullptr, BK_MEM_TAG_GFX, canvas, (isize)sizeof(BK_GfxCanvas));
}

BK_GfxTexture *bk_gfx_canvas_texture(BK_GfxCanvas *canvas) {
  BK_ASSERT(canvas != nullptr);
  return canvas->color;
}

void bk_gfx_canvas_size(const BK_GfxCanvas *canvas, i32 *out_width, i32 *out_height) {
  BK_ASSERT(canvas != nullptr);
  if (out_width != nullptr) {
    *out_width = canvas->width;
  }
  if (out_height != nullptr) {
    *out_height = canvas->height;
  }
}

SDL_GPUTexture *bk__gfx_canvas_depth_handle(const BK_GfxCanvas *canvas) {
  BK_ASSERT(canvas != nullptr);
  if (canvas->depth == nullptr) {
    return nullptr;
  }
  return bk__gfx_texture_handle(canvas->depth);
}

SDL_GPUTextureFormat bk__gfx_canvas_depth_format(const BK_GfxCanvas *canvas) {
  BK_ASSERT(canvas != nullptr);
  if (canvas->depth == nullptr) {
    return SDL_GPU_TEXTUREFORMAT_INVALID;
  }
  return bk__gfx_texture_format(canvas->depth);
}

SDL_GPUFilter bk__gfx_canvas_blit_filter(const BK_GfxCanvas *canvas) {
  BK_ASSERT(canvas != nullptr);
  return canvas->blit_filter;
}
