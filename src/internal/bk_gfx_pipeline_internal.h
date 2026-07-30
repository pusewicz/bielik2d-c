#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_pipeline.h>

/// Returns the underlying SDL_GPU pipeline handle. Framework-internal; used by
/// bk_gfx's frame flush to bind the pipeline pending from bk_gfx_bind_pipeline
/// (added in a later task).
SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline);
