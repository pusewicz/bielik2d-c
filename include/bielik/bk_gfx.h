#pragma once

typedef struct BK_GfxPipeline BK_GfxPipeline;

/// RGBA color.
typedef struct BK_Color {
    float r, g, b, a;
} BK_Color;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);

/// Binds a pipeline to be used by the next bk_gfx_draw call this frame. The
/// binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Issues a draw of vertex_count vertices using the most recently bound pipeline.
/// Must be called after bk_gfx_bind_pipeline in the same frame.
void bk_gfx_draw(int vertex_count);

/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again
/// each frame you want captured. Failures (bad path, unsupported swapchain
/// composition) are logged via SDL_Log with a "BK: " prefix, not returned -- the
/// actual capture happens later, inside the frame's flush.
void bk_gfx_request_capture(const char *path);
