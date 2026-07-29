#pragma once
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_pipeline.h>

/// Returns the color most recently set via bk_gfx_set_clear_color, or the
/// default {0.1, 0.1, 0.12, 1.0} if it hasn't been called yet.
BK_Color bk__gfx_get_clear_color(void);

/// Acquires the swapchain texture, clears it to the color set via
/// bk_gfx_set_clear_color, and presents. Called once per frame by the
/// frame pipeline. No-op (submits an empty command buffer) if the
/// swapchain texture isn't available (minimized/occluded window).
void bk__gfx_flush(void);

/// Test-only accessor: returns the pipeline bound via bk_gfx_bind_pipeline this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxPipeline *bk__gfx_get_pending_pipeline(void);

/// Test-only accessor: returns the vertex count set via bk_gfx_draw this frame, or
/// 0 if bk_gfx_draw hasn't been called since the last flush.
int bk__gfx_get_pending_vertex_count(void);
