#pragma once
#include <bielik/bk_gfx_pipeline.h>

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
