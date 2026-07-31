# Bielik2D — Implementation Plan: Phase 0 (Scaffold) + Phase 1 (App Core)

This document is the working spec for the first two phases of Bielik2D, a 2D game
framework in C23 built on SDL3. It is written to be executed by Claude Code.

## 0. How to use this document (instructions to the implementing agent)

- Read the entire document before writing any code.
- Execute phases in order; within a phase, execute numbered tasks in order.
- After every task: build, run tests, fix warnings. Commit per task with a short
  imperative message (`phase1: implement bk_clock advance`).
- Public API signatures in section 6.1 are **normative**. Do not rename, reorder,
  or "improve" them. If you find a genuine defect, implement the fix and record
  it in `DEVIATIONS.md` at the repo root with one paragraph of rationale.
- Consult section 8 (Non-Goals) before adding anything not listed in a task. If
  it is in section 8, do not build it, even partially, even behind a flag.
- Do not add third-party dependencies beyond those named in this document.
- Prefer the smallest implementation that satisfies the acceptance checklist.
  Leave `// TODO(phaseN):` markers where later phases will extend.

## 1. Project context

Bielik2D is a streamlined 2D game framework replacing the author's use of Cute
Framework. Philosophy:

- **SDL3-native.** SDL is a hard, permanent dependency. SDL types (e.g.
  `SDL_Event`) appear directly in the public API where wrapping adds nothing.
- **Off-the-shelf where possible.** Custom code is reserved for the draw layer,
  app glue, and asset plumbing. Later phases bring in Box2D v3, PhysFS,
  minicoro, SDL_ttf, Dear ImGui (via C bindings), cgltf.
- **Data-driven.** Configuration is data (C23 designated-initializer descriptor
  structs now; loadable descs later).
- **Deterministic-sim friendly.** Fixed timestep with a `u64` tick counter as
  the determinism anchor; integer-nanosecond time internals.
- **Single-threaded v1, multithread-shaped.** No threads or locks in v1. The
  task-system interface, handle discipline, and record/flush rendering split
  exist so threading can be added later without API breaks.

Target games are 2D (first dogfood: the author's game *Space Delivery*), with
3D as a later option.

## 2. Hard constraints

- Language: **C23**. Compilers: Clang >= 18, GCC >= 14, AppleClang >= 16,
  clang-cl on Windows. **MSVC is unsupported** — CMake must fail configure on
  `MSVC` compiler ID with a message directing to clang-cl.
- C++ is forbidden in the library. (A single quarantined C++ TU arrives with
  ImGui in a later phase; not now.)
- SDL: **3.4.8**, pinned, via CMake FetchContent (release zip, not git tip).
- No other runtime dependencies in Phases 0–1.
- Warnings: `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla`; `-Werror` in CI.
- All framework heap allocation goes through `SDL_malloc`/`SDL_free` wrappers
  (`bk__alloc`/`bk__free`, internal) so `SDL_SetMemoryFunctions` covers
  everything. The frame arena (6.6) sits on top.
- License: **zlib**. `LICENSE` file in Phase 0. When code is later ported from
  Cute Framework (also zlib/public-domain), attribution goes in `NOTICE.md`.

## 3. Conventions (Phase 0 extracts this section verbatim into `CLAUDE.md`)

Naming:
- Public functions: `bk_` + snake_case (`bk_run`, `bk_frame_alloc`).
- Public types: `BK_` + PascalCase (`BK_AppDesc`, `BK_FrameInfo`).
- Enum values: `BK_` + UPPER_SNAKE (`BK_CONTINUE`).
- Internal linker-visible symbols: `bk__` prefix. File-static functions: `s_` prefix.
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).

C23 usage:
- Use: `bool`/`true`/`false`, `nullptr`, designated initializers, compound
  literals, `constexpr` for constants, `static_assert`, `[[nodiscard]]` on
  functions returning `BK_Result`, `[[maybe_unused]]`, `typeof` where it
  removes duplication.
- Avoid: VLAs (`-Wvla` enforces), `alloca`, `_Generic` unless clearly better,
  `auto` outside obvious initializers, bit-precise ints in public API.
- `#embed` is reserved for later phases (needs Clang 19+/GCC 15); do not use yet.

Style:
- `.clang-format` (Phase 0): LLVM base, 4-space indent, 100 columns,
  `PointerAlignment: Right` (`char *p`), K&R attached braces. Run on everything.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant. Terse; no boilerplate prose.
- Public headers must each compile standalone (enforced by test, 6.9).
- Errors: no silent failure. Boot-path failures log via `SDL_Log` with a
  `"BK: "` prefix and return `BK_FAIL`. Assertions: `BK_ASSERT` wraps
  `SDL_assert`.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.

Process:
- Never reorganize the file layout beyond section 4.
- Keep functions small; no premature abstraction; no speculative options.

## 4. Repository layout

```
bielik2d/
  CMakeLists.txt
  cmake/warnings.cmake
  .clang-format
  .github/workflows/ci.yml
  LICENSE            (zlib)
  NOTICE.md          (empty scaffold with heading)
  CLAUDE.md          (generated from section 3)
  PLAN.md            (this file)
  DEVIATIONS.md      (created empty)
  include/bielik/
    bk_app.h         (result, frame info, desc, run, accessors)
    bk_main.h        (entry-point shim; the ONLY header that includes SDL_main.h)
    bk_time.h        (pure clock/accumulator)
    bk_task.h        (task-system interface + serial executor)
    bk_gfx.h         (Phase 1: BK_Color + clear color only)
  src/
    bk_app.c
    bk_time.c
    bk_task.c
    bk_gfx.c
    internal/bk_app_internal.h
    internal/bk_gfx_internal.h
    internal/bk_task_internal.h
  samples/
    CMakeLists.txt
    01_clear/main.c
    01_clear/CMakeLists.txt
    02_ticks/main.c
    02_ticks/CMakeLists.txt
  tests/
    CMakeLists.txt
    bk_test.h        (tiny assertion macros; one executable per test file)
    test_header_*.c  (five generated stubs, standalone-compilation check, see 6.9)
    test_version.c
    test_time.c
    test_task.c
    test_arena.c
    test_gfx.c
    test_app_lifecycle.c
```

## 5. Phase 0 — Scaffold

Tasks:
1. `CMakeLists.txt`: `cmake_minimum_required(3.28)`, project `bielik2d` C only,
   `CMAKE_C_STANDARD 23` + `REQUIRED ON`, fail on MSVC with clang-cl message.
   Options `BK_BUILD_SAMPLES` (ON), `BK_BUILD_TESTS` (ON). Static library
   target `bielik`, alias `Bielik2D::bielik`. FetchContent SDL 3.4.8 from the
   release zip URL, `SDL_SHARED OFF`, `SDL_STATIC ON`, tests/examples off.
2. `cmake/warnings.cmake`: warning set from section 2, `BK_WERROR` option
   (default OFF, ON in CI). Debug builds add `-fsanitize=address,undefined`
   behind option `BK_SANITIZE` (default ON for Debug on Linux/macOS).
3. `.clang-format`, `LICENSE` (zlib, current year, copyright holder marked
   `TODO: set holder` — decide whether that's you personally or your SLU),
   `NOTICE.md`, `DEVIATIONS.md`, `CLAUDE.md` (verbatim section 3).
4. CI (`ci.yml`): matrix = ubuntu-latest (clang), macos-latest (AppleClang),
   windows-latest (clang-cl via `-T ClangCL` or Ninja+clang-cl). Steps:
   configure (Release + `BK_WERROR=ON`), build, run ctest. Ubuntu additionally
   installs `mesa-vulkan-drivers xvfb` and runs `xvfb-run samples/01_clear
   --frames 120` as a smoke step (`continue-on-error: true` — see 6.10).
5. Placeholder `bk_app.h` with `BK_VERSION_MAJOR/MINOR/PATCH` (0.1.0) and a
   `bk_version_string()` so the library links; placeholder test proving ctest
   wiring.

Acceptance: CI green on all three OS with an empty-ish library; `ctest` runs.

## 6. Phase 1 — App core

### 6.1 Public API (normative)

`include/bielik/bk_app.h` — essential content (add doc comments per section 3;
`BK_API` export macro may be introduced but is `extern` for the static lib):

```c
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

typedef enum BK_Result {
    BK_CONTINUE = 0,   // == SDL_APP_CONTINUE
    BK_DONE     = 1,   // == SDL_APP_SUCCESS
    BK_FAIL     = 2,   // == SDL_APP_FAILURE
} BK_Result;
// static_assert value-equality with SDL_AppResult lives in bk_app.c.

typedef struct BK_FrameInfo {
    uint64_t tick;       // fixed ticks since boot; determinism anchor
    double   sim_time;   // tick * fixed_dt (recomputed, never accumulated)
    double   real_time;  // wall-clock seconds since bk boot
    double   dt;         // update/post_update: fixed_dt. render: frame delta
    double   alpha;      // render only: [0,1) interpolation factor (1.0 in variable mode)
} BK_FrameInfo;

typedef struct BK_WindowDesc {
    const char *title;   // default "Bielik2D"
    int  w, h;           // default 1280x720
    bool resizable;
    bool fullscreen;
    bool vsync;          // true => VSYNC present mode, false => IMMEDIATE (fallback VSYNC)
} BK_WindowDesc;

typedef struct BK_TimeDesc {
    int    tick_hz;              // 0 => variable-dt mode (default)
    int    max_ticks_per_frame;  // default 8; spiral-of-death cap
    double max_frame_dt;         // default 0.25s; hitch clamp (debugger, window drag)
} BK_TimeDesc;

// Task system: shapes match Box2D v3 (b2TaskCallback / enqueue / finish) so a
// real scheduler (e.g. enkiTS) can pass through unchanged in a later phase.
typedef void (*BK_TaskFn)(int32_t start, int32_t end, uint32_t worker_index, void *arg);

typedef struct BK_TaskSystemDesc {  // all-zero => built-in serial executor
    void *ctx;
    void *(*enqueue)(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg, void *ctx);
    void  (*finish)(void *task, void *ctx);
} BK_TaskSystemDesc;

typedef struct BK_AppDesc {
    BK_WindowDesc     window;
    BK_TimeDesc       time;
    BK_TaskSystemDesc tasks;
    BK_Result (*init)(void **state, int argc, char **argv); // *state pre-seeded from .userdata
    BK_Result (*update)(void *state, const BK_FrameInfo *f);       // fixed step (or once/frame in variable mode)
    BK_Result (*post_update)(void *state, const BK_FrameInfo *f);  // optional; runs after physics slot
    void      (*render)(void *state, const BK_FrameInfo *f);       // once per frame
    BK_Result (*event)(void *state, const SDL_Event *e);           // optional; see 6.3 quit rules
    void      (*quit)(void *state, BK_Result result);              // optional
    void *userdata;
} BK_AppDesc;

// Runs the app. Blessed path is the BK_APP macro (bk_main.h). bk_run directly
// is supported on native only in v1 (tools/tests); treat as noreturn on web.
int bk_run(const BK_AppDesc *desc, int argc, char **argv);

// Valid between init and quit:
SDL_Window    *bk_window(void);
SDL_GPUDevice *bk_gpu(void);

// Per-frame linear allocator; reset after render/flush each frame. Never free.
void *bk_frame_alloc(size_t size, size_t align);

// Convenience: pushes SDL_EVENT_QUIT (equivalent to returning BK_DONE).
void bk_quit(void);

const char *bk_version_string(void);
```

`include/bielik/bk_main.h`:

```c
#pragma once
// Include from exactly ONE translation unit (the one defining BK_APP or main).
//
// Default mode (BK_APP): SDL provides the platform entry point and drives the
// SDL3 main-callbacks machinery; trampolines below forward into the library.
//
// #define BK_MAIN_HANDLED before including to write your own main() and call
// bk_run() yourself (native tools/tests only in v1).

#ifndef BK_MAIN_HANDLED
    #define SDL_MAIN_USE_CALLBACKS 1
#endif
#include <SDL3/SDL_main.h>
#include <bielik/bk_app.h>

#define BK_APP(...) \
    BK_AppDesc bk__app_desc(void) { return (BK_AppDesc){ __VA_ARGS__ }; }

#ifndef BK_MAIN_HANDLED
// Trampolines emitted into the user's TU; bodies forward to library internals
// (bk__boot / bk__iterate / bk__event / bk__shutdown declared in bk_app.h or
// an internal header included here). SDL_AppInit calls bk__boot(bk__app_desc,
// argc, argv, appstate). Each returns the mapped SDL_AppResult.
#endif
```

(Exact trampoline bodies: four non-static functions `SDL_AppInit`,
`SDL_AppIterate`, `SDL_AppEvent`, `SDL_AppQuit`, each one line, casting
`BK_Result` to `SDL_AppResult`. The library-internal entry points they call are
declared in a small public-but-underscored block at the bottom of `bk_app.h`.)

`include/bielik/bk_time.h` — pure, SDL-free logic (takes timestamps as
parameters; unit-testable without initializing SDL):

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct BK_Clock {
    uint64_t fixed_dt_ns;         // 0 => variable mode
    uint64_t max_frame_ns;        // hitch clamp
    int      max_ticks_per_frame; // spiral cap
    uint64_t accumulator_ns;
    uint64_t last_now_ns;
    uint64_t tick;                // total fixed ticks since init
    bool     started;
} BK_Clock;

typedef struct BK_ClockFrame {
    int    ticks;      // fixed updates to run this frame (always 1 in variable mode)
    double frame_dt;   // clamped wall delta, seconds
    double alpha;      // accumulator / fixed_dt in [0,1); 1.0 in variable mode
} BK_ClockFrame;

void          bk_clock_init(BK_Clock *c, int tick_hz, int max_ticks_per_frame,
                            double max_frame_dt, uint64_t now_ns);
BK_ClockFrame bk_clock_advance(BK_Clock *c, uint64_t now_ns);
double        bk_clock_fixed_dt(const BK_Clock *c);   // seconds; 0.0 in variable mode
double        bk_clock_sim_time(const BK_Clock *c);   // tick * fixed_dt, in double
```

`include/bielik/bk_task.h`:

```c
#pragma once
#include <bielik/bk_app.h>

// Runs fn over [0,count) honoring the app's task system (serial in v1).
// Blocks until complete. min_range is a splitting hint for real schedulers.
void bk_task_run(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg);
```

`include/bielik/bk_gfx.h` (Phase 1 surface only):

```c
#pragma once

typedef struct BK_Color { float r, g, b, a; } BK_Color;

void bk_gfx_set_clear_color(BK_Color color);
```

### 6.2 Frame pipeline (normative)

Executed by `bk__iterate()` (called from the `SDL_AppIterate` trampoline).
Single-threaded. Stage order is fixed; there is no user-extensible scheduler.

```
now = SDL_GetTicksNS()
cf  = bk_clock_advance(&app.clock, now)

for i in 0..cf.ticks:
    // input snapshot slot — Phase 4 inserts bk_input__begin_tick() here
    info = { tick, sim_time, real_time, dt = fixed_dt (or cf.frame_dt in variable mode), alpha = 0 }
    r = desc.update(state, &info);        if r != BK_CONTINUE -> terminate(r)
    // physics slot — Phase 8: bk_physics__step() runs here iff bk_physics_init was called
    if desc.post_update:
        r = desc.post_update(state, &info); if r != BK_CONTINUE -> terminate(r)
    app.clock.tick advanced inside bk_clock_advance's contract (see 6.4)

info.alpha = cf.alpha; info.dt = cf.frame_dt
if desc.render: desc.render(state, &info)
bk_gfx__flush()          // Phase 1: acquire swapchain, clear, present (6.7)
bk__arena_reset()
return BK_CONTINUE
```

Rules:
- Callbacks receive `const BK_FrameInfo *`; the struct may grow, signatures never change.
- `render` must not mutate sim state (documented contract, not enforced in v1).
- Nothing in the pipeline calls `SDL_PollEvent` — SDL's callback machinery owns
  the queue and delivers via the event trampoline.

### 6.3 Entry-point plumbing

Library internals (defined in `bk_app.c`, declared in an underscored block at
the bottom of `bk_app.h`):

- `SDL_AppResult bk__boot(BK_AppDesc (*get_desc)(void), void **appstate, int argc, char **argv)`
  1. Copy desc; apply defaults (window 1280x720/"Bielik2D"; time: max_ticks 8,
     max_frame_dt 0.25; tasks: serial executor if all-zero).
  2. `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)`.
  3. Create window per desc (`SDL_WINDOW_RESIZABLE`/fullscreen flags; the
     window is also created with `SDL_WINDOW_HIDDEN`, shown after GPU claim to
     avoid a white flash).
  4. `SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
     SDL_GPU_SHADERFORMAT_MSL, /*debug*/ true in Debug builds, NULL)`;
     `SDL_ClaimWindowForGPUDevice`; `SDL_SetGPUSwapchainParameters` with
     `SDL_GPU_SWAPCHAINCOMPOSITION_SDR` and present mode from `.vsync`
     (IMMEDIATE requested but fall back to VSYNC if unsupported via
     `SDL_WindowSupportsGPUPresentMode`).
  5. Init clock (`bk_clock_init` with `SDL_GetTicksNS()`), arena, gfx state.
  6. Seed `*appstate = desc.userdata`; call `desc.init(appstate, argc, argv)`.
  7. On any failure: `SDL_Log("BK: ...")`, plus
     `SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, ...)` in Release builds,
     and return `SDL_APP_FAILURE`.
- `SDL_AppResult bk__iterate(void *appstate)` — section 6.2.
- `SDL_AppResult bk__event(void *appstate, SDL_Event *e)` — quit rules:
  - If `desc.event` is set: forward everything; the user owns quit handling
    (returning `BK_DONE` on `SDL_EVENT_QUIT`, etc.). No double handling.
  - If `desc.event` is NULL: built-in handling returns `BK_DONE` on
    `SDL_EVENT_QUIT`; everything else ignored.
- `void bk__shutdown(void *appstate, SDL_AppResult result)` — call `desc.quit`
  if set **and** boot reached a point where `desc.init` had a chance to
  populate `*appstate` (i.e. `bk__boot` didn't fail before or during `init`);
  otherwise `appstate` is still whatever it was before `init` ran (typically
  nullptr) and calling `quit` with it would violate every sample's assumption
  that `state` is a valid pointer once `quit` runs. Then release GPU swapchain
  claim, destroy device, destroy window. (SDL calls `SDL_Quit` itself after
  `SDL_AppQuit` returns.)

`bk_run(desc, argc, argv)`: store desc in a static, then
`SDL_EnterAppMainCallbacks(argc, argv, <static trampolines in bk_app.c>)` that
feed the same four internals. Return its result. Document as noreturn on
Emscripten; v1 does not support `bk_run` on web (the `BK_APP` path will, in
Phase 11).

`BK_APP` path linkage: the user's TU (via `bk_main.h`) defines the four
`SDL_App*` symbols as one-line trampolines; `bk__app_desc` is defined by the
`BK_APP` macro in the same TU; the library never references `bk__app_desc`
directly (only through the function pointer passed to `bk__boot`), so `bk_run`
users don't need it to exist. No weak symbols anywhere.

### 6.4 Time semantics (bk_time.c — pure functions, no SDL calls)

- All internal arithmetic in `uint64_t` nanoseconds. `fixed_dt_ns =
  1'000'000'000 / tick_hz` (integer division; document the sub-ns remainder as
  irrelevant because the accumulator consumes real wall time).
- `bk_clock_advance`:
  1. First call after init: `frame_dt_ns = 0`.
  2. `raw = now - last_now; frame_dt_ns = min(raw, max_frame_ns); last_now = now`.
  3. Variable mode (`fixed_dt_ns == 0`): `ticks = 1`, `tick += 1`,
     `alpha = 1.0`, `frame_dt = ns_to_sec(frame_dt_ns)`. Done.
  4. Fixed mode: `accumulator += frame_dt_ns`; `ticks = min(accumulator /
     fixed_dt_ns, max_ticks_per_frame)`; `accumulator -= ticks * fixed_dt_ns`;
     if the cap was hit, clamp `accumulator = min(accumulator, fixed_dt_ns)`
     (drop backlog; running slow beats death-spiraling). `tick += ticks`.
     `alpha = accumulator / (double)fixed_dt_ns` — guaranteed `[0,1)`.
- `sim_time` is always `tick * (1.0 / tick_hz)` recomputed in double — never a
  running float sum.
- Monotonicity guard: if `now < last_now` (should not happen with
  `SDL_GetTicksNS`, but be defensive), treat `raw = 0`.

Note for 6.2: the pipeline consumes `cf.ticks` and reads per-tick `tick` values
as `clock.tick - cf.ticks + i + 1`-style bookkeeping — implement however is
cleanest, but `BK_FrameInfo.tick` during update `i` must equal the global tick
count as of *completing* that update's step, and render's `tick` equals the
last completed tick.

### 6.5 Task system

- `bk_task_run` looks up the app's `BK_TaskSystemDesc`; calls
  `enqueue(fn, count, min_range, arg, ctx)` then `finish(task, ctx)`.
- Built-in serial executor: `enqueue` calls `fn(0, count, 0, arg)` immediately
  and returns a non-NULL sentinel (`(void *)1`); `finish` is a no-op.
  `min_range` is ignored (splitting hint for real schedulers).
- Must be callable before `init` returns and until `quit` returns.
- If `count <= 0`, return without calling `fn`.

### 6.6 Frame arena

- Single linear allocator; default capacity 4 MiB, grows by doubling (log on
  growth — growth indicates a leak-ish usage pattern worth seeing).
- `bk_frame_alloc(size, align)`: align must be a power of two; alignment
  fallback `alignof(max_align_t)` when 0 is passed. Out-of-memory after growth
  failure: `BK_ASSERT` in Debug, return NULL in Release (documented).
- Reset (pointer rewind, memory retained) at end of frame after `gfx flush`.
- Usable from `init` (arena exists before user init runs; reset first happens
  at end of first frame).

### 6.7 GPU bring-up (Phase 1 scope of bk_gfx)

Phase 1 gfx is intentionally minimal: prove the device, swapchain, and present
loop. The record/flush draw-list architecture is Phase 3; do not scaffold it.

`bk_gfx__flush()` per frame:
1. `cmd = SDL_AcquireGPUCommandBuffer(device)`; on NULL, log once and return.
2. `SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &tex, NULL, NULL)`; if
   `tex == NULL` (minimized/occluded), `SDL_SubmitGPUCommandBuffer(cmd)` and
   return.
3. Begin render pass with one color target: `load_op = SDL_GPU_LOADOP_CLEAR`,
   `clear_color` from `bk_gfx_set_clear_color` state (default {0.1,0.1,0.12,1}),
   `store_op = SDL_GPU_STOREOP_STORE`. End pass immediately.
4. `SDL_SubmitGPUCommandBuffer(cmd)`.

No shaders, pipelines, buffers, or textures in Phase 1.

### 6.8 Samples

- `samples/01_clear`: `BK_APP` with variable-dt mode; render callback cycles the
  clear color via `sinf(real_time)`; event callback quits on `SDL_EVENT_QUIT`
  and ESC key-down. Accepts `--frames N` (parsed in init from argv): when set,
  update returns `BK_DONE` after N frames — used by CI smoke.
- `samples/02_ticks`: `tick_hz = 60`, `max_ticks_per_frame = 8`. Prints one
  line per second summarizing: renders/sec, ticks/sec, current alpha, drift
  between `sim_time` and `real_time`. Demonstrates hitch clamp by sleeping
  300 ms when SPACE is pressed (via event callback setting a flag; the sleep
  happens in update — with a comment saying this is deliberately naughty).
  Also accepts `--frames N`.

Both samples are also documentation: heavy comments, ideal usage style.

### 6.9 Tests

Harness `tests/bk_test.h`: `BK_TEST_MAIN` + `REQUIRE(cond)` / `REQUIRE_EQ_U64`
/ `REQUIRE_NEAR(a,b,eps)` macros; failures print file:line and the expression,
process exits nonzero. One executable per test file, registered with CTest. No
registration magic, no SDL init required for pure tests.

- `test_headers.c`: includes every public header in isolation via separate
  `#include` + recompilation trick: simplest portable approach is one tiny TU
  per header compiled as additional CMake object targets
  (`test_header_bk_app.c` containing only `#include <bielik/bk_app.h>` etc.).
  Generate these five stub files; the "test" is that they compile.
- `test_time.c` (the important one — exhaustive):
  - fixed 60 Hz, synthetic 16.666667 ms steps: over 100k frames, total ticks
    within ±1 of expected; alpha always in `[0,1)`; `sim_time` drift vs
    accumulated wall input < 1 fixed step.
  - hitch: one 2 s gap with `max_frame_dt = 0.25` produces exactly
    `min(0.25/fixed_dt, cap)` ticks and clamped accumulator.
  - spiral cap: feed 100 ms frames at 60 Hz cap 8 -> exactly 8 ticks/frame and
    accumulator clamped to `< fixed_dt` after each advance.
  - variable mode: ticks always 1, dt equals clamped input, alpha == 1.0.
  - first-frame dt == 0; non-monotonic input treated as 0.
- `test_task.c`: serial executor covers full range exactly once, in-order;
  `count = 0` and negative count call nothing; worker_index is 0; custom desc
  pass-through invoked correctly.
- `test_arena.c`: alignment honored for 1..4096 pow2; reset rewinds; growth
  works; interleaved sizes don't overlap (fill patterns).

### 6.10 Phase 1 acceptance checklist

- [ ] CI green: build + ctest on ubuntu (clang), macos, windows (clang-cl),
      `-Werror`, ASan/UBSan clean on the test suite (Linux/macOS Debug job).
- [ ] `xvfb-run samples/01_clear --frames 120` exits 0 on ubuntu CI with
      `mesa-vulkan-drivers` (lavapipe). This step is `continue-on-error: true`
      the first time; if it proves stable across 3 consecutive runs, remove the
      escape hatch and make it required. If lavapipe cannot create a GPU device
      in CI, record the failure mode in DEVIATIONS.md and leave the step
      allow-fail — do NOT add a null-gfx code path to force it green.
- [ ] Both samples run on a real desktop: window opens, color animates, ESC and
      close button quit, `02_ticks` reports ~60 ticks/sec at any refresh rate,
      hitch key doesn't explode tick count afterwards.
- [ ] `BK_MAIN_HANDLED` + `bk_run` path exercised by building `01_clear` a
      second time as `01_clear_run` with a 10-line alternate main.
- [ ] No public API deviations from 6.1, or each is documented in
      DEVIATIONS.md.
- [ ] Every public symbol doc-commented; clang-format clean.

## 7. Roadmap after Phase 1 (context only — each gets its own plan doc)

- P2  gfx core: pipelines, buffers, textures, offline shader compile
      (SDL_shadercross toolchain), canvases/render targets.
- P3  draw2d: record/flush draw list, sprite batch, atlas, SDF shapes
      (port Cute Framework's approach; zlib attribution in NOTICE.md).
- P4  input module: pending-event buffer, per-tick snapshots, polled API
      (`bk_key_down` etc.), gamepads. Snapshot slot from 6.2 activates.
- P5  text: SDL_ttf GPU text engine.
- P6  VFS + assets: PhysFS mounts, asset DB, hot reload polling, `#embed`
      fallbacks.
- P7  audio: miniaudio (or SDL3_mixer — decision pending).
- P8  physics: Box2D v3 behind `bk_physics_init`; task desc passes through;
      pipeline physics slot activates.
- P9  coroutines: minicoro wrapper.
- P10 debug UI: Dear ImGui via C bindings, single quarantined C++ TU.
- P11 web: Emscripten target; graphics backend decision point.
- P12 continuous: Space Delivery port begins as soon as P3 lands.

## 8. Non-goals — do NOT implement any of these now

- Threads, atomics, locks, thread pools, a real task scheduler.
- Render thread, double-buffered draw lists, draw-list recording of any kind.
- Generic user-extensible pipeline stages or a job/system scheduler.
- ECS, entities, components, scene graphs.
- Input snapshotting/replay (slot exists; code does not).
- Interpolation helpers (alpha is delivered; using it is the game's job).
- Config files, save systems, logging frameworks, string/hashmap containers.
- Networking (permanently out of scope for the framework).
- Multi-window, HDR swapchain composition, resize handling beyond what SDL
  gives for free (explicit resize events handling arrives with P2).
- Any abstraction over SDL types "for portability".

## 9. Decision log (do not relitigate in implementation sessions)

- SDL3 main-callbacks are the only loop model: Emscripten requires chunked
  operation and iOS wants main to return; SDL fakes callbacks with a loop
  elsewhere, so there is no cost on desktop.
- `BK_Result` mirrors `SDL_AppResult` values so trampolines are casts.
- Raw `SDL_Event` in the public API: SDL is a permanent dependency; wrapping
  its event zoo is high-LOC, zero-value.
- Descriptor struct + `BK_APP` over link-time user symbols: config-as-data,
  hot-reload-friendly (function pointers are swappable), testable (`bk_run`
  callable from tools), one-app-per-binary limitation avoided.
- Task interface copies Box2D v3's shapes exactly so P8 passes through with
  zero adaptation; serial executor makes it a no-op today.
- Integer-nanosecond clock with `tick` as `u64` anchor: determinism for a
  future replay/seeded-run story; float accumulation drift eliminated by
  construction.
- Backlog dropped when the spiral cap hits: running slow is recoverable,
  a death spiral is not.
- MSVC excluded: its C23 support lags badly; clang-cl covers Windows.
- Phase 1 deliberately ships no draw list so P3 can design it against real
  sprite-batch requirements instead of speculation.
