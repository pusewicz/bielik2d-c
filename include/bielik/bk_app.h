#pragma once
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

#define BK_VERSION_MAJOR 0
#define BK_VERSION_MINOR 1
#define BK_VERSION_PATCH 0

/// Returns the library version as "MAJOR.MINOR.PATCH".
const char *bk_version_string(void);

/// Runs fn over a sub-range of [0,count) on one worker. Shape matches Box2D
/// v3's task callback so a real scheduler can plug in without adaptation.
typedef void (*BK_TaskFn)(int32_t start, int32_t end, uint32_t worker_index, void *arg);

/// Describes a pluggable task system. All-zero (every field NULL) means: use
/// the built-in single-threaded serial executor.
typedef struct BK_TaskSystemDesc {
    void *ctx;
    void *(*enqueue)(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg, void *ctx);
    void (*finish)(void *task, void *ctx);
} BK_TaskSystemDesc;

/// Debug-build assertion; compiles to nothing meaningful in Release beyond
/// what SDL_assert itself does. Wraps SDL_assert so all framework asserts
/// share one breakpoint/logging behavior.
#define BK_ASSERT(cond) SDL_assert(cond)

/// Per-frame linear allocator; reset after render/flush each frame. Never
/// free individual allocations — the whole arena rewinds at frame end.
/// align must be a power of two, or 0 to use the platform's max alignment.
void *bk_frame_alloc(size_t size, size_t align);

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
    uint64_t tick;    // fixed ticks since boot; determinism anchor
    double sim_time;  // tick * fixed_dt (recomputed, never accumulated)
    double real_time; // wall-clock seconds since bk boot
    double dt;        // update/post_update: fixed_dt. render: frame delta
    double alpha;     // render only: [0,1) interpolation factor (1.0 in variable mode)
} BK_FrameInfo;

/// Window creation parameters.
typedef struct BK_WindowDesc {
    const char *title; // default "Bielik2D"
    int w, h;          // default 1280x720
    bool resizable;
    bool fullscreen;
    bool vsync; // true => VSYNC present mode, false => IMMEDIATE (fallback VSYNC)
} BK_WindowDesc;

/// Fixed/variable timestep configuration.
typedef struct BK_TimeDesc {
    int tick_hz;             // 0 => variable-dt mode (default)
    int max_ticks_per_frame; // default 8; spiral-of-death cap
    double max_frame_dt;     // default 0.25s; hitch clamp (debugger, window drag)
} BK_TimeDesc;

/// Full app configuration: window/time/task setup and the callback set
/// bk_run (or the BK_APP macro) drives the app lifecycle through.
typedef struct BK_AppDesc {
    BK_WindowDesc window;
    BK_TimeDesc time;
    BK_TaskSystemDesc tasks;
    BK_Result (*init)(void **state, int argc, char **argv); // *state pre-seeded from .userdata
    BK_Result (*update)(void *state,
                        const BK_FrameInfo *f); // fixed step (or once/frame in variable mode)
    BK_Result (*post_update)(void *state,
                             const BK_FrameInfo *f);     // optional; runs after physics slot
    void (*render)(void *state, const BK_FrameInfo *f);  // once per frame
    BK_Result (*event)(void *state, const SDL_Event *e); // optional; see quit rules below
    void (*quit)(void *state, BK_Result result);         // optional
    void *userdata;
} BK_AppDesc;

/// Runs the app. Blessed path is the BK_APP macro (bk_main.h). Calling this
/// directly is supported on native only in v1 (tools/tests). Returns a
/// process exit code (0 on clean termination, 1 on failure), not a
/// BK_Result.
int bk_run(const BK_AppDesc *desc, int argc, char **argv);

/// Valid between init and quit.
SDL_Window *bk_window(void);

/// Valid between init and quit.
SDL_GPUDevice *bk_gpu(void);

/// Convenience: pushes SDL_EVENT_QUIT. With no custom .event handler, this
/// is equivalent to returning BK_DONE; with a custom handler, the app
/// decides how (or whether) to respond to the queued event.
void bk_quit(void);

// Internal entry points; called by bk_main.h's trampolines. Not for direct use.
SDL_AppResult bk__boot(BK_AppDesc (*get_desc)(void), void **appstate, int argc, char **argv);
SDL_AppResult bk__iterate(void *appstate);
SDL_AppResult bk__event(void *appstate, SDL_Event *event);
void bk__shutdown(void *appstate, SDL_AppResult result);
