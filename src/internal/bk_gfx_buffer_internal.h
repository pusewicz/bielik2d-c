#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_buffer.h>

/// Returns the underlying SDL_GPU buffer handle. Framework-internal; used by bk_gfx's
/// frame flush to bind the buffer pending from bk_gfx_bind_vertex_buffer/
/// bk_gfx_bind_index_buffer (added in a later task), and by compute dispatch to bind
/// read-only storage buffers.
SDL_GPUBuffer *bk__gfx_buffer_handle(const BK_GfxBuffer *buffer);

/// Returns the byte size buffer was created with. Framework-internal; test-only
/// accessor.
uint32_t bk__gfx_buffer_size(const BK_GfxBuffer *buffer);
