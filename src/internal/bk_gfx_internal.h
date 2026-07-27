#pragma once
#include <bielik/bk_gfx.h>

/// Returns the color most recently set via bk_gfx_set_clear_color, or the
/// default {0.1, 0.1, 0.12, 1.0} if it hasn't been called yet.
BK_Color bk__gfx_get_clear_color(void);
