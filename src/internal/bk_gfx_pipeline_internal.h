#pragma once
#include <bielik/bk_gfx_pipeline.h>

#include <SDL3/SDL_gpu.h>

/// Returns the underlying SDL_GPU pipeline handle. Framework-internal; used by
/// bk_gfx's frame flush to bind the pipeline pending from bk_gfx_bind_pipeline
/// (added in a later task).
SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline);

/// Returns the pipeline's declared depth_stencil_format (BK_GfxPipelineDesc field,
/// SDL_GPU_TEXTUREFORMAT_INVALID if none). Framework-internal; used by bk_gfx's frame
/// flush to assert a bound pipeline's declared depth attachment matches the render
/// pass it's drawn into -- SDL_GPU's own mismatch error is an opaque validation
/// failure with no reference to which call caused it.
SDL_GPUTextureFormat bk__gfx_pipeline_depth_format(const BK_GfxPipeline *pipeline);

/// Returns the pipeline's declared color_target_format (BK_GfxPipelineDesc field).
/// Framework-internal; used by bk_gfx's frame flush to assert a bound pipeline's
/// declared colour attachment matches the render pass it's drawn into.
SDL_GPUTextureFormat bk__gfx_pipeline_color_format(const BK_GfxPipeline *pipeline);
