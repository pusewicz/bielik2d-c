#pragma once
#include <bielik/bk_types.h>

/// Resets the frame arena (rewinds every backing block's cursor; the blocks
/// themselves are retained and recycled, not freed). Called once per frame
/// after gfx flush by the frame pipeline — exposed here so tests can
/// exercise the reset/rewind behavior without a running app.
void bk__arena_reset(void);

/// Frees every one of the frame arena's backing blocks. Called once by
/// bk__shutdown.
void bk__arena_free(void);
