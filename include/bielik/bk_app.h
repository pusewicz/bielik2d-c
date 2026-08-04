#pragma once
#include <bielik/bk_alloc.h>
#include <bielik/bk_types.h>

#include <SDL3/SDL.h>

#define BK_VERSION_MAJOR 0
#define BK_VERSION_MINOR 1
#define BK_VERSION_PATCH 0

/// Returns the library version as "MAJOR.MINOR.PATCH".
const char *bk_version_string(void);

/// Runs fn over a sub-range of [0,count) on one worker. Shape matches Box2D
/// v3's task callback so a real scheduler can plug in without adaptation.
typedef void (*BK_TaskFn)(i32 start, i32 end, u32 worker_index, void *arg);

/// Describes a pluggable task system. All-zero (every field NULL) means: use
/// the built-in single-threaded serial executor.
typedef struct BK_TaskSystemDesc {
  void *ctx;
  void *(*enqueue)(BK_TaskFn fn, i32 count, i32 min_range, void *arg, void *ctx);
  void (*finish)(void *task, void *ctx);
} BK_TaskSystemDesc;

// ---------------------------------------------------------------------------
// Assertions
//
// Three tiers, mapping 1:1 onto SDL's assertion levels so they share one
// breakpoint/logging/handler behavior. The level is pinned by CMake (BK_ASSERT_LEVEL,
// defaulting to 2 in Debug and 1 otherwise), NOT inferred from optimization flags --
// see the comment on it in the top-level CMakeLists.txt for why that matters.
//
// A failed assertion runs SDL's default handler, which pops a modal GUI dialog. Set
// the SDL_ASSERT environment variable to "abort" (or "break"/"ignore") for anything
// with no user to dismiss it; the test suite does this via CTest.
//
// Note that a disabled assertion does NOT evaluate its condition -- SDL wraps it in
// sizeof -- so never put required work inside one.
// ---------------------------------------------------------------------------

/// Framework assertion, live in Release as well as Debug (SDL assert level >= 1).
/// This is the default tier and what nearly every check should use: most of the
/// framework's asserts guard a pointer that the very next line dereferences, so
/// compiling them out trades a loud abort for undefined behavior.
#define BK_ASSERT(cond) SDL_assert_release(cond)

/// Debug-only assertion (SDL assert level >= 2), for a check too expensive to keep in
/// Release. Deliberately has no call sites yet -- it is the migration target for when
/// profiling shows a specific BK_ASSERT is hot, not dead code.
#define BK_ASSERT_DEBUG(cond) SDL_assert(cond)

/// Opt-in expensive assertion, off in Debug and Release alike; only live at SDL assert
/// level 3 (-DBK_ASSERT_LEVEL=3). For checks worth running when hunting a specific bug
/// but far too costly to leave on. Deliberately has no call sites yet.
#define BK_ASSERT_PARANOID(cond) SDL_assert_paranoid(cond)

/// Per-frame linear allocator; reset after render/flush each frame. Never
/// free individual allocations — the whole arena rewinds at frame end.
/// align must be a power of two, or 0 to use the platform's max alignment.
/// Every pointer returned stays valid until the end of the frame, including
/// across later bk_frame_alloc calls that exhaust the current capacity: the
/// arena grows by adding backing blocks, never by moving the ones already
/// handed out. So a per-frame structure may safely hold arena pointers and
/// write through them later in the same frame.
void *bk_frame_alloc(usize size, usize align);

/// Termination/continuation code returned by app callbacks; numerically
/// identical to SDL_AppResult so the entry-point trampolines can forward it
/// as-is.
typedef enum BK_Result {
  BK_CONTINUE = 0, // == SDL_APP_CONTINUE
  BK_DONE = 1,     // == SDL_APP_SUCCESS
  BK_FAIL = 2,     // == SDL_APP_FAILURE
} BK_Result;

// static_assert value-equality with SDL_AppResult lives in bk_app.c.

/// Per-frame timing snapshot passed to update/post_update/render.
typedef struct BK_FrameInfo {
  u64 tick;      // fixed ticks since boot; determinism anchor
  f64 sim_time;  // tick * fixed_dt (recomputed, never accumulated)
  f64 real_time; // wall-clock seconds since bk boot
  f64 dt;        // update/post_update: fixed_dt. render: frame delta
  f64 alpha;     // render only: [0,1) interpolation factor (1.0 in variable mode)
} BK_FrameInfo;

/// Window creation parameters.
typedef struct BK_WindowDesc {
  const char *title; // default "Bielik2D"
  i32 width, height; // default 1280x720
  bool resizable;
  bool fullscreen;
  bool vsync;         // true => VSYNC present mode, false => IMMEDIATE (fallback VSYNC)
  bool depth_stencil; // true => the swapchain render pass gets a depth-stencil
                      // attachment sized to the drawable, recreated on resize. See
                      // bk_gfx_pipeline.h's BK_GfxPipelineDesc.depth_stencil_format.
} BK_WindowDesc;

/// Fixed/variable timestep configuration.
typedef struct BK_TimeDesc {
  i32 tick_hz;             // 0 => variable-dt mode (default)
  i32 max_ticks_per_frame; // default 8; spiral-of-death cap
  f64 max_frame_dt;        // default 0.25s; hitch clamp (debugger, window drag)
} BK_TimeDesc;

/// Full app configuration: window/time/task setup and the callback set
/// bk_run (or the BK_APP macro) drives the app lifecycle through.
///
/// Setting .allocator also routes SDL's own internal allocations through it, but only if
/// nothing has called into SDL yet when bk_run starts: SDL allocations made earlier carry
/// none of the bookkeeping the framework's shim needs, so boot logs and leaves SDL on its
/// own allocator rather than risk them. Any SDL call before bk_run costs the routing --
/// SDL_SetHint (including BK_HINT_NO_ERROR_DIALOG below) allocates. Set such hints from the
/// environment instead to keep it, and expect a second bk_run in one process never to route,
/// since SDL retains allocations past SDL_Quit.
typedef struct BK_AppDesc {
  BK_WindowDesc window;
  BK_TimeDesc time;
  BK_TaskSystemDesc tasks;
  BK_Allocator allocator; // base allocator for all framework allocation; all-zero => libc heap
  BK_Result (*init)(void **state, int argc, char **argv); // *state pre-seeded from .userdata
  BK_Result (*update)(void *state,
                      const BK_FrameInfo *frame); // fixed step (or once/frame in variable mode)
  BK_Result (*post_update)(void *state,
                           const BK_FrameInfo *frame);     // optional; runs after physics slot
  void (*render)(void *state, const BK_FrameInfo *frame);  // once per frame
  BK_Result (*event)(void *state, const SDL_Event *event); // optional; see quit rules below
  void (*quit)(void *state, BK_Result result);             // optional
  void *userdata;
} BK_AppDesc;

/// An SDL hint: set to "1" to suppress the error dialog a Release build shows when
/// boot fails, leaving only the SDL_Log message. Release builds pop that dialog so a
/// double-clicked game explains itself instead of vanishing, but it blocks forever
/// with nobody to dismiss it in automated tests, CI, and headless runs. Settable
/// programmatically via SDL_SetHint before bk_run, or as an environment variable of
/// the same name -- SDL hints resolve from the environment too. No effect in Debug
/// builds, which never show the dialog.
#define BK_HINT_NO_ERROR_DIALOG "BK_NO_ERROR_DIALOG"

/// Runs the app. Blessed path is the BK_APP macro (bk_main.h). Calling this
/// directly is supported on native only in v1 (tools/tests). Returns a
/// process exit code (0 on clean termination, 1 on failure), not a
/// BK_Result.
int bk_run(const BK_AppDesc *desc, int argc, char **argv);

/// Valid between init and quit.
SDL_Window *bk_window(void);

/// Valid between init and quit.
SDL_GPUDevice *bk_gpu(void);

/// Writes the window's current drawable size in pixels to *out_w/*out_h. Equal to
/// its logical size today -- Bielik2D doesn't request a high-DPI window, so points
/// == pixels (DPI-aware scaling is deferred). Refreshed on SDL_EVENT_WINDOW_RESIZED
/// and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, before any app .event handler runs, so
/// it's always current by the time app code observes a resize. Valid between init
/// and quit.
void bk_window_size(i32 *out_w, i32 *out_h);

/// Convenience: pushes SDL_EVENT_QUIT. With no custom .event handler, this
/// is equivalent to returning BK_DONE; with a custom handler, the app
/// decides how (or whether) to respond to the queued event.
void bk_quit(void);

// Internal entry points; called by bk_main.h's trampolines. Not for direct use.
SDL_AppResult bk__boot(BK_AppDesc (*get_desc)(void), void **appstate, int argc, char **argv);
SDL_AppResult bk__iterate(void *appstate);
SDL_AppResult bk__event(void *appstate, SDL_Event *event);
void bk__shutdown(void *appstate, SDL_AppResult result);
