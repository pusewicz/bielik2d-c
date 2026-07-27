#pragma once

/// RGBA color.
typedef struct BK_Color {
    float r, g, b, a;
} BK_Color;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);
