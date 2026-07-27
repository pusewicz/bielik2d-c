#include "internal/bk_gfx_internal.h"
#include <bielik/bk_gfx.h>

static BK_Color s_clear_color = {0.1f, 0.1f, 0.12f, 1.0f};

void bk_gfx_set_clear_color(BK_Color color) { s_clear_color = color; }

BK_Color bk__gfx_get_clear_color(void) { return s_clear_color; }
