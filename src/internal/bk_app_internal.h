#pragma once
#include <bielik/bk_types.h>

/// Allocates size bytes via SDL_malloc. Framework-internal; all framework
/// heap allocation must go through this (and bk__free) so
/// SDL_SetMemoryFunctions covers everything.
void *bk__alloc(usize size);

/// Reallocates via SDL_realloc. See bk__alloc.
void *bk__realloc(void *ptr, usize size);

/// Frees memory allocated via bk__alloc/bk__realloc.
void bk__free(void *ptr);

/// Resets the frame arena (rewinds the allocation pointer; backing memory
/// is retained, not freed). Called once per frame after gfx flush by the
/// frame pipeline (a later task) — exposed here so this task's test can
/// exercise the reset/rewind behavior without a running app.
void bk__arena_reset(void);

/// Frees the frame arena's backing allocation. Called once by bk__shutdown.
void bk__arena_free(void);
