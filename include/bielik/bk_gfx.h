#pragma once
#include <bielik/bk_types.h>

typedef struct BK_GfxPipeline BK_GfxPipeline;
typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;
typedef struct BK_GfxSampler BK_GfxSampler;
typedef struct BK_GfxCanvas BK_GfxCanvas;

/// RGBA color.
typedef struct BK_Color {
  f32 r, g, b, a;
} BK_Color;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);

/// Binds a pipeline to be used by the next bk_gfx_draw/bk_gfx_draw_indexed call this
/// frame. The binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Issues a draw of vertex_count vertices using the most recently bound pipeline.
/// Must be called after bk_gfx_bind_pipeline in the same frame.
void bk_gfx_draw(i32 vertex_count);

/// Binds a vertex buffer to slot 0 for the next draw call this frame. The binding is
/// consumed (cleared) by the frame's flush.
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);

/// Binds an index buffer for the next bk_gfx_draw_indexed call this frame. Indices
/// are always read as 16-bit (see bk_gfx_buffer.h's BK_GFX_BUFFER_USAGE_INDEX). The
/// binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);

/// Binds a texture and sampler pair to fragment slot 0 for the next draw call this
/// frame. The binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);

/// Issues an indexed draw of index_count indices using the most recently bound
/// pipeline, vertex buffer, and index buffer. Must be called after
/// bk_gfx_bind_pipeline/bk_gfx_bind_vertex_buffer/bk_gfx_bind_index_buffer in the
/// same frame.
void bk_gfx_draw_indexed(i32 index_count);

/// Renders this frame into canvas instead of the swapchain, then blits the canvas
/// onto the swapchain (stretched to fit, filtered per the canvas's blit_filter) once
/// the render pass ends -- a canvas smaller than the window is the fixed-internal-
/// resolution / pixel-art path. The binding is consumed (cleared) by the frame's
/// flush; pass nullptr (or don't call this) to render into the swapchain directly, as
/// before. A pipeline bound this frame must have been created with
/// BK_GfxPipelineDesc.depth_stencil_format matching canvas's depth attachment (or
/// SDL_GPU_TEXTUREFORMAT_INVALID if canvas has none) -- BK_ASSERTs otherwise.
void bk_gfx_bind_canvas(BK_GfxCanvas *canvas);

/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again
/// each frame you want captured. Failures (bad path, unsupported swapchain
/// composition) are logged via SDL_Log with a "BK: " prefix, not returned -- the
/// actual capture happens later, inside the frame's flush. If bk_gfx_bind_canvas was
/// also called this frame, the capture happens after the canvas is blitted onto the
/// swapchain, so it captures the canvas's contents, not a blank clear.
void bk_gfx_request_capture(const char *path);
