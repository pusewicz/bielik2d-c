# Fundamental Types & Identifier Readability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce `bk_types.h`'s short fixed-width type aliases (`i32`, `u32`, `f32`, ...), migrate every existing numeric type and single-letter identifier in the codebase to use them, and lock the result in with a `clang-tidy` check.

**Architecture:** No new behavior anywhere — every step in this plan is TDD's *refactor* phase: tests stay green throughout, never red-then-green. There is deliberately no "write a failing test" step in any task; the existing test suite is the regression check. Because every alias in `bk_types.h` is a transparent typedef (`u32` *is* `uint32_t` *is* SDL's `Uint32`), a half-migrated file compiles and passes tests exactly like a fully-migrated one — so Task 10's grep gate, not a green build, is what proves the migration is actually complete.

**Tech Stack:** C23, CMake, clang-everywhere (clang, clang-cl, AppleClang), SDL3/SDL_GPU, `-Wall -Wextra -Wshadow -Wvla -Werror` (via `-DBK_WERROR=ON`), `clang-tidy` (new).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-30-fundamental-types-design.md` (read it first — this plan assumes its §2–§9).
- Naming: bare type aliases (`i32`, not `bk_i32`/`BK_I32`) — deliberate, see spec §3.
- Single-letter identifiers are banned everywhere in this codebase except `i`, `x`, `y`, `r`, `g`, `b`, `a` (spec §6).
- Every task's build must be clean under `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build` — this includes `-Wshadow`, which is the actual risk for the `clock` rename in Task 1.
- Every task's affected tests must pass via `ctest --test-dir build --output-on-failure -R <regex>`.
- Bundle type migration and identifier renames per file in one task/commit — they touch the same lines.
- Commit messages: human voice, no Conventional Commits prefixes, no AI signoffs (see this repo's own git log for style).
- Task 1 must land first — it creates `bk_types.h`, which every other task's files include. Tasks 2–8 are then mutually independent (each header adds its own explicit `#include <bielik/bk_types.h>`, so none of them depend on another task's landing order — every `bk_types.h` alias is a transparent typedef for its stdint/float/double/size_t equivalent, so even an unmigrated neighbor header stays compatible). Task 9 (samples) is the one real exception: it depends on Tasks 2, 4, 5, 6, and 7 already being merged, since the samples call their public APIs directly and rely on transitive `bk_types.h` includes rather than adding their own. Tasks 10 and 11 are the final verification/enforcement pass and must run last. This makes Tasks 2–8 safe for subagent-driven parallel execution; Task 9 is not safe to start until 2/4/5/6/7 are in.

---

## Task 1: `bk_types.h` + `bk_time` migration (spike — de-risks `clock` for every later task)

This task is deliberately first and deliberately bundles a new header with a full module migration: the `BK_Clock *c` → `BK_Clock *clock` rename is the one unproven identifier in this whole effort (`clock()` is a `<time.h>` libc symbol, and `-Wshadow -Werror` is already enforced project-wide). Six later tasks assume this name is safe. Prove it here, first, instead of assuming it.

**Files:**
- Create: `include/bielik/bk_types.h`
- Create: `tests/test_header_bk_types.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/bielik/bk_time.h`
- Modify: `src/bk_time.c`
- Modify: `tests/test_time.c`

**Interfaces:**
- Produces: `i8/i16/i32/i64`, `u8/u16/u32/u64`, `f32/f64`, `usize/isize`, `b32` — every later task consumes these by name, unqualified (bare, no `bk_`/`BK_` prefix).
- Produces: `void bk_clock_init(BK_Clock *clock, i32 tick_hz, i32 max_ticks_per_frame, f64 max_frame_dt, u64 now_ns)`, `BK_ClockFrame bk_clock_advance(BK_Clock *clock, u64 now_ns)`, `f64 bk_clock_fixed_dt(const BK_Clock *clock)`, `f64 bk_clock_sim_time(const BK_Clock *clock)` — Task 2's `bk_app.c` consumes these exact signatures.
- Produces: `BK_Clock { u64 fixed_dt_ns; u64 max_frame_ns; i32 max_ticks_per_frame; u64 accumulator_ns; u64 last_now_ns; u64 tick; bool started; }`, `BK_ClockFrame { i32 ticks; f64 frame_dt; f64 alpha; }` — Task 2's `bk_app.c` reads `s_app.clock.tick` (still named `tick`) and `cf.ticks`/`cf.frame_dt`/`cf.alpha`.

- [ ] **Step 1: Create `include/bielik/bk_types.h`**

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

// Bare, unprefixed names (Rust/Zig/Odin-style) traded deliberately against
// bielik2d's usual bk_/BK_ namespacing -- see the design spec's §3 for the
// rationale and the collision risk this accepts.

/// 8/16/32/64-bit signed integers.
typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

/// 8/16/32/64-bit unsigned integers.
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/// IEEE-754 single/double precision floats.
typedef float  f32;
typedef double f64;

/// Pointer-width unsigned/signed sizes (aliases of size_t / ptrdiff_t).
typedef size_t    usize;
typedef ptrdiff_t isize;

/// 32-bit boolean for GPU/shader-layout structs (std140/std430 alignment rules
/// don't allow C's 1-byte bool). NOT a replacement for bool in ordinary control
/// flow -- use bool/true/false for that.
typedef int32_t b32;

static_assert(sizeof(i8) == 1, "i8 must be 1 byte");
static_assert(sizeof(i16) == 2, "i16 must be 2 bytes");
static_assert(sizeof(i32) == 4, "i32 must be 4 bytes");
static_assert(sizeof(i64) == 8, "i64 must be 8 bytes");
static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");
static_assert(sizeof(b32) == 4, "b32 must be 4 bytes");
```

- [ ] **Step 2: Create `tests/test_header_bk_types.c`**

```c
#include <bielik/bk_types.h>
```

(Matches the existing one-line pattern in every other `test_header_bk_*.c` file exactly — this is the standalone-compilation check, not a runtime test.)

- [ ] **Step 3: Register the new header stub in `tests/CMakeLists.txt`**

Add after the `test_header_bk_time` block:

```cmake
add_library(test_header_bk_types OBJECT test_header_bk_types.c)
target_link_libraries(test_header_bk_types PRIVATE bielik bk_warnings)
```

- [ ] **Step 4: Build and confirm the new header compiles standalone**

Run: `cmake --build build --target test_header_bk_types`
Expected: builds clean, no warnings (this alone proves the `static_assert`s pass on this machine's ABI).

- [ ] **Step 5: Migrate `include/bielik/bk_time.h`**

Replace the whole file:

```c
#pragma once
#include <bielik/bk_types.h>
#include <stdbool.h>

/// Fixed/variable timestep accumulator state; owns no resources.
typedef struct BK_Clock {
    u64 fixed_dt_ns;    // 0 => variable mode
    u64 max_frame_ns;   // hitch clamp
    i32 max_ticks_per_frame; // spiral cap
    u64 accumulator_ns;
    u64 last_now_ns;
    u64 tick; // total fixed ticks since init
    bool started;
} BK_Clock;

/// Result of one bk_clock_advance call: ticks to run this frame and the
/// render-interpolation alpha.
typedef struct BK_ClockFrame {
    i32 ticks;       // fixed updates to run this frame (always 1 in variable mode)
    f64 frame_dt; // clamped wall delta, seconds
    f64 alpha;    // accumulator / fixed_dt in [0,1); 1.0 in variable mode
} BK_ClockFrame;

/// Initializes a clock for either fixed-tick (tick_hz > 0) or variable-dt (tick_hz == 0) mode.
/// max_frame_dt <= 0.0 is treated as unset and substituted with 0.25; max_ticks_per_frame < 1
/// is substituted with 1 — both would otherwise be nonsensical (a frozen clock or a silently
/// disabled spiral-of-death cap).
void bk_clock_init(BK_Clock *clock, i32 tick_hz, i32 max_ticks_per_frame, f64 max_frame_dt,
                   u64 now_ns);
/// Advances the clock to now_ns, returning the fixed ticks to run and interpolation alpha.
BK_ClockFrame bk_clock_advance(BK_Clock *clock, u64 now_ns);
/// Returns the fixed timestep in seconds, or 0.0 in variable mode.
f64 bk_clock_fixed_dt(const BK_Clock *clock); // seconds; 0.0 in variable mode
/// Returns tick * fixed_dt, recomputed in double each call (never accumulated).
f64 bk_clock_sim_time(const BK_Clock *clock); // tick * fixed_dt, in double
```

(`<stdint.h>` is dropped — nothing in this header needs it once `bk_types.h` covers the fixed-width types; `<stdbool.h>` stays for `bool`/`true`/`false`, per CLAUDE.md's C23 conventions.)

- [ ] **Step 6: Migrate `src/bk_time.c`**

Replace the whole file:

```c
#include <bielik/bk_time.h>

void bk_clock_init(BK_Clock *clock, i32 tick_hz, i32 max_ticks_per_frame, f64 max_frame_dt,
                   u64 now_ns) {
    if (max_frame_dt <= 0.0) {
        // Casting a negative double to uint64_t is undefined behavior, and
        // 0.0 itself would clamp every frame's dt to zero forever (raw_ns <
        // 0 is never true for an unsigned raw_ns) — both are nonsensical
        // for a hitch clamp, so treat them as "unset" and fall back to the
        // same 0.25s default BK_TimeDesc documents and bk__boot substitutes
        // for the exactly-zero case.
        max_frame_dt = 0.25;
    }
    if (max_ticks_per_frame < 1) {
        // A negative cap wraps to ~1.8e19 when cast to uint64_t below,
        // silently disabling the spiral-of-death cap entirely.
        max_ticks_per_frame = 1;
    }

    clock->fixed_dt_ns = tick_hz > 0 ? 1000000000ULL / (u64)tick_hz : 0;
    clock->max_frame_ns = (u64)(max_frame_dt * 1e9);
    clock->max_ticks_per_frame = max_ticks_per_frame;
    clock->accumulator_ns = 0;
    clock->tick = 0;
    clock->started = false;
    clock->last_now_ns = now_ns;
}

BK_ClockFrame bk_clock_advance(BK_Clock *clock, u64 now_ns) {
    u64 frame_dt_ns;

    if (!clock->started) {
        clock->started = true;
        clock->last_now_ns = now_ns;
        frame_dt_ns = 0;
    } else {
        u64 raw_ns = now_ns < clock->last_now_ns ? 0 : now_ns - clock->last_now_ns;
        frame_dt_ns = raw_ns < clock->max_frame_ns ? raw_ns : clock->max_frame_ns;
        clock->last_now_ns = now_ns;
    }

    if (clock->fixed_dt_ns == 0) {
        clock->tick += 1;
        return (BK_ClockFrame){
            .ticks = 1,
            .frame_dt = (f64)frame_dt_ns / 1e9,
            .alpha = 1.0,
        };
    }

    clock->accumulator_ns += frame_dt_ns;
    u64 uncapped_ticks = clock->accumulator_ns / clock->fixed_dt_ns;
    u64 ticks = uncapped_ticks < (u64)clock->max_ticks_per_frame
                         ? uncapped_ticks
                         : (u64)clock->max_ticks_per_frame;
    clock->accumulator_ns -= ticks * clock->fixed_dt_ns;
    if (uncapped_ticks > (u64)clock->max_ticks_per_frame) {
        clock->accumulator_ns %= clock->fixed_dt_ns;
    }
    clock->tick += ticks;

    return (BK_ClockFrame){
        .ticks = (i32)ticks,
        .frame_dt = (f64)frame_dt_ns / 1e9,
        .alpha = (f64)clock->accumulator_ns / (f64)clock->fixed_dt_ns,
    };
}

f64 bk_clock_fixed_dt(const BK_Clock *clock) {
    return clock->fixed_dt_ns == 0 ? 0.0 : (f64)clock->fixed_dt_ns / 1e9;
}

f64 bk_clock_sim_time(const BK_Clock *clock) { return (f64)clock->tick * bk_clock_fixed_dt(clock); }
```

- [ ] **Step 7: Migrate `tests/test_time.c`**

Every `BK_Clock c`/`c2`/`vc` local becomes `clock`/`clock2`/`vclock`; every bare `BK_ClockFrame f`/`f1`/`f2`/`vf` stays (2-letter names like `f1`/`f2`/`vf` are fine — only the single-letter `f` is in scope, and this file doesn't have a bare `f`). Apply these substitutions (the file's structure is otherwise unchanged — same test functions, same assertions, same numeric literals):

Replace every occurrence of `BK_Clock c;` → `BK_Clock clock;`, `BK_Clock c2;` → `BK_Clock clock2;`, `BK_Clock vc;` → `BK_Clock vclock;`, and every subsequent reference to `c`/`c2`/`vc` (as the first argument to `bk_clock_init`/`bk_clock_advance`/`bk_clock_fixed_dt`/`bk_clock_sim_time`, or as `c.tick`/`c.accumulator_ns`/`c2....`) to `clock`/`clock2`/`vclock` respectively. Concretely:

```c
#include "bk_test.h"
#include <bielik/bk_time.h>

static void test_fixed_60hz_steady_steps(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, 0.25, 0);

    const u64 frame_ns = 16666667ULL;
    const int frames = 100000;
    u64 now_ns = 0;
    u64 total_fed_ns = 0;

    for (int i = 0; i < frames; i++) {
        now_ns += frame_ns;
        BK_ClockFrame f = bk_clock_advance(&clock, now_ns);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
        if (i > 0) {
            total_fed_ns += frame_ns;
        }
    }

    const u64 fixed_dt_ns = 1000000000ULL / 60ULL;
    const u64 expected_ticks = total_fed_ns / fixed_dt_ns;
    const u64 diff =
        clock.tick > expected_ticks ? clock.tick - expected_ticks : expected_ticks - clock.tick;
    REQUIRE(diff <= 1);

    const double sim_time = bk_clock_sim_time(&clock);
    const double wall_s = (double)total_fed_ns / 1e9;
    REQUIRE_NEAR(sim_time, wall_s, 1.0 / 60.0);
}

static void test_hitch_clamp(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, 0.25, 0);
    bk_clock_advance(&clock, 0);

    u64 now_ns = 2000000000ULL;
    BK_ClockFrame f = bk_clock_advance(&clock, now_ns);

    const int uncapped_hitch_ticks = (int)(0.25 / (1.0 / 60.0));
    const int expected_hitch_ticks = uncapped_hitch_ticks < 8 ? uncapped_hitch_ticks : 8;
    REQUIRE(f.ticks == expected_hitch_ticks);
    REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);

    now_ns += 1000000000ULL / 60ULL;
    BK_ClockFrame f2 = bk_clock_advance(&clock, now_ns);
    REQUIRE(f2.ticks == 1);
    REQUIRE(f2.alpha >= 0.0 && f2.alpha < 1.0);
}

static void test_spiral_cap_sustained_load(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, 0.25, 0);
    bk_clock_advance(&clock, 0);

    u64 now_ns = 0;

    const u64 below_cap_frame_ns = 100000000ULL;
    for (int i = 0; i < 10; i++) {
        now_ns += below_cap_frame_ns;
        BK_ClockFrame f = bk_clock_advance(&clock, now_ns);
        REQUIRE(f.ticks == 6);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
    }

    const u64 above_cap_frame_ns = 200000000ULL;
    for (int i = 0; i < 10; i++) {
        now_ns += above_cap_frame_ns;
        BK_ClockFrame f = bk_clock_advance(&clock, now_ns);
        REQUIRE(f.ticks == 8);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
    }
}

static void test_variable_mode(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 0, 8, 0.25, 0);
    bk_clock_advance(&clock, 0);

    const u64 deltas_ns[] = {10000000ULL, 33000000ULL, 8000000ULL, 50000000ULL, 400000000ULL};
    const usize count = sizeof(deltas_ns) / sizeof(deltas_ns[0]);
    const double max_frame_dt = 0.25;

    u64 now_ns = 0;
    for (usize i = 0; i < count; i++) {
        now_ns += deltas_ns[i];
        BK_ClockFrame f = bk_clock_advance(&clock, now_ns);
        REQUIRE(f.ticks == 1);
        const double input_s = (double)deltas_ns[i] / 1e9;
        const double expected_dt = input_s < max_frame_dt ? input_s : max_frame_dt;
        REQUIRE_NEAR(f.frame_dt, expected_dt, 1e-9);
        REQUIRE(f.alpha == 1.0);
    }

    REQUIRE(bk_clock_fixed_dt(&clock) == 0.0);
    REQUIRE(bk_clock_sim_time(&clock) == 0.0);
}

static void test_first_frame_dt_zero(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, 0.25, 1000000000000ULL);
    BK_ClockFrame f = bk_clock_advance(&clock, 5000000000000ULL);
    REQUIRE(f.frame_dt == 0.0);
    REQUIRE(f.ticks == 0);
    REQUIRE(f.alpha == 0.0);

    BK_Clock vclock;
    bk_clock_init(&vclock, 0, 8, 0.25, 1000000000000ULL);
    BK_ClockFrame vf = bk_clock_advance(&vclock, 5000000000000ULL);
    REQUIRE(vf.frame_dt == 0.0);
    REQUIRE(vf.ticks == 1);
    REQUIRE(vf.alpha == 1.0);
}

static void test_non_monotonic_input(void) {
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, 0.25, 0);
    bk_clock_advance(&clock, 0);

    const u64 forward_ns = 5000000ULL;
    BK_ClockFrame f1 = bk_clock_advance(&clock, forward_ns);
    REQUIRE(f1.ticks == 0);

    const u64 backlog_before = clock.accumulator_ns;
    const u64 backward_ns = forward_ns - 1000000ULL;
    BK_ClockFrame f2 = bk_clock_advance(&clock, backward_ns);
    REQUIRE(f2.ticks == 0);
    REQUIRE_EQ_U64(clock.accumulator_ns, backlog_before);
    const double fixed_dt_ns_d = (double)(1000000000ULL / 60ULL);
    REQUIRE_NEAR(f2.alpha, (double)backlog_before / fixed_dt_ns_d, 1e-12);

    BK_Clock vclock;
    bk_clock_init(&vclock, 0, 8, 0.25, 0);
    bk_clock_advance(&vclock, 0);
    bk_clock_advance(&vclock, 20000000ULL);
    BK_ClockFrame vf = bk_clock_advance(&vclock, 10000000ULL);
    REQUIRE(vf.ticks == 1);
    REQUIRE(vf.frame_dt == 0.0);
    REQUIRE(vf.alpha == 1.0);
}

static void test_negative_timedesc_fields_clamped(void) {
    // max_frame_dt < 0: without clamping, (uint64_t)(negative * 1e9) is UB
    // and in practice yields max_frame_ns == 0, which then clamps every
    // frame's dt to zero forever (raw_ns < 0 is never true for an unsigned
    // raw_ns). Confirm a normal input delta still produces a sane, non-zero
    // frame_dt instead of getting stuck at zero.
    BK_Clock clock;
    bk_clock_init(&clock, 60, 8, -1.0, 0);
    bk_clock_advance(&clock, 0);

    const u64 normal_frame_ns = 16666667ULL;
    BK_ClockFrame f = bk_clock_advance(&clock, normal_frame_ns);
    REQUIRE(f.frame_dt > 0.0);
    REQUIRE_NEAR(f.frame_dt, (double)normal_frame_ns / 1e9, 1e-9);

    // max_ticks_per_frame < 0: without clamping, (uint64_t)max_ticks_per_frame
    // wraps to ~1.8e19, so the spiral-of-death cap check (uncapped_ticks >
    // cap) is never true and the cap silently stops capping. Confirm a huge
    // synthetic gap still caps at a small positive tick count, not thousands.
    BK_Clock clock2;
    bk_clock_init(&clock2, 60, -5, 0.25, 0);
    bk_clock_advance(&clock2, 0);

    BK_ClockFrame f2 = bk_clock_advance(&clock2, 10000000000ULL); // 10 real seconds
    REQUIRE(f2.ticks == 1);                                   // max_ticks_per_frame clamps up to 1
    REQUIRE(f2.alpha >= 0.0 && f2.alpha < 1.0);
}

int main(void) {
    test_fixed_60hz_steady_steps();
    test_hitch_clamp();
    test_spiral_cap_sustained_load();
    test_variable_mode();
    test_first_frame_dt_zero();
    test_non_monotonic_input();
    test_negative_timedesc_fields_clamped();
    printf("test_time: OK\n");
    return 0;
}
```

(`<stddef.h>` is dropped from the includes — `size_t` isn't used anymore, `usize` is, via `bk_types.h` which `bk_time.h` already pulls in transitively; `printf` still resolves via `bk_test.h`'s own `<stdio.h>` include.)

- [ ] **Step 8: Build with `-DBK_WERROR=ON` and confirm `-Wshadow` is silent**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
Expected: clean build, zero warnings. This is the actual test of the `clock` rename risk — if `-Wshadow` fires on any `BK_Clock *clock` parameter or local, stop and report it before continuing to any other task (every later task assumes this name is safe).

- [ ] **Step 9: Run the affected tests**

Run: `ctest --test-dir build --output-on-failure -R 'test_time|test_header_bk_time|test_header_bk_types'`
Expected: all pass, unchanged assertions/output from before this task.

- [ ] **Step 10: Commit**

```bash
git add include/bielik/bk_types.h tests/test_header_bk_types.c tests/CMakeLists.txt \
        include/bielik/bk_time.h src/bk_time.c tests/test_time.c
git commit -m "$(cat <<'EOF'
add bk_types.h and migrate bk_time to it

Short fixed-width aliases (i32/u32/f32/...) replace stdint.h's verbose
names; BK_Clock's *c param becomes *clock now that -Wshadow -Werror
confirms it doesn't collide with <time.h>'s clock().
EOF
)"
```

---

## Task 2: `bk_app` migration (core: types, `frame`/`event`/`result` renames, `BK_WindowDesc.width`/`.height`)

The biggest single task — `bk_app.h` is the hub every other module and every sample/test includes. Also fixes the spec's corrected finding: `BK_WindowDesc { int w, h; }` was never actually spelled out despite an earlier draft's claim, so it renames here alongside the type migration.

**Files:**
- Modify: `include/bielik/bk_app.h`
- Modify: `src/internal/bk_app_internal.h`
- Modify: `src/bk_app.c`
- Modify: `tests/test_app_lifecycle.c`

**Interfaces:**
- Consumes: `bk_clock_init`/`bk_clock_advance`/`bk_clock_fixed_dt`/`bk_clock_sim_time` with the `i32`/`u64`/`f64` signatures from Task 1.
- Produces: `BK_TaskFn(i32 start, i32 end, u32 worker_index, void *arg)`, `BK_TaskSystemDesc.enqueue(BK_TaskFn fn, i32 count, i32 min_range, void *arg, void *ctx)` — Task 3 (`bk_task`) consumes these exact signatures.
- Produces: `void *bk_frame_alloc(usize size, usize align)`, `void *bk__alloc(usize size)`, `void *bk__realloc(void *ptr, usize size)` — no other task calls these directly, but any future allocation-adjacent code should match.
- Produces: `BK_FrameInfo` with `u64 tick`, `f64 sim_time/real_time/dt/alpha`; callback signatures `update`/`post_update`/`render` take `const BK_FrameInfo *frame`, `event` takes `const SDL_Event *event` — Tasks 4–9's samples and tests implement these callbacks and must use the parameter name `frame`/`event`, not `f`/`e`.
- Produces: `BK_WindowDesc { const char *title; i32 width, height; bool resizable, fullscreen, vsync; }` — Task 9's samples don't touch this directly (none set `.width`/`.height` today), but `tests/test_app_lifecycle.c` (this task) and `tests/test_gfx_capture.c` (Task 4) do.

- [ ] **Step 1: Migrate `include/bielik/bk_app.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL.h>
#include <bielik/bk_types.h>

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

/// Debug-build assertion; compiles to nothing meaningful in Release beyond
/// what SDL_assert itself does. Wraps SDL_assert so all framework asserts
/// share one breakpoint/logging behavior.
#define BK_ASSERT(cond) SDL_assert(cond)

/// Per-frame linear allocator; reset after render/flush each frame. Never
/// free individual allocations — the whole arena rewinds at frame end.
/// align must be a power of two, or 0 to use the platform's max alignment.
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
    u64 tick;    // fixed ticks since boot; determinism anchor
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
    bool vsync; // true => VSYNC present mode, false => IMMEDIATE (fallback VSYNC)
} BK_WindowDesc;

/// Fixed/variable timestep configuration.
typedef struct BK_TimeDesc {
    i32 tick_hz;             // 0 => variable-dt mode (default)
    i32 max_ticks_per_frame; // default 8; spiral-of-death cap
    f64 max_frame_dt;        // default 0.25s; hitch clamp (debugger, window drag)
} BK_TimeDesc;

/// Full app configuration: window/time/task setup and the callback set
/// bk_run (or the BK_APP macro) drives the app lifecycle through.
typedef struct BK_AppDesc {
    BK_WindowDesc window;
    BK_TimeDesc time;
    BK_TaskSystemDesc tasks;
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
```

(`argc`/`argv` stay plain `int`/`char **` throughout — that's the C/SDL main-callback convention, not a data type this effort touches. `bk_run`'s `int` return stays too: it's a process exit code, the same C-runtime convention `main()` itself uses.)

- [ ] **Step 2: Migrate `src/internal/bk_app_internal.h`**

Replace the whole file:

```c
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
```

- [ ] **Step 3: Migrate `src/bk_app.c`**

Replace the whole file:

```c
#define SDL_MAIN_HANDLED 1

#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_task_internal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bielik/bk_app.h>
#include <bielik/bk_time.h>

#define BK_STR_(x) #x
#define BK_STR(x) BK_STR_(x)

static_assert(BK_CONTINUE == (BK_Result)SDL_APP_CONTINUE, "BK_Result must mirror SDL_AppResult");
static_assert(BK_DONE == (BK_Result)SDL_APP_SUCCESS, "BK_Result must mirror SDL_AppResult");
static_assert(BK_FAIL == (BK_Result)SDL_APP_FAILURE, "BK_Result must mirror SDL_AppResult");

const char *bk_version_string(void) {
    return BK_STR(BK_VERSION_MAJOR) "." BK_STR(BK_VERSION_MINOR) "." BK_STR(BK_VERSION_PATCH);
}

void *bk__alloc(usize size) { return SDL_malloc(size); }

void *bk__realloc(void *ptr, usize size) { return SDL_realloc(ptr, size); }

void bk__free(void *ptr) { SDL_free(ptr); }

static constexpr usize s_arena_default_capacity = 4 * 1024 * 1024;

static struct {
    unsigned char *base;
    usize capacity;
    usize used;
} s_frame_arena;

void bk__arena_reset(void) { s_frame_arena.used = 0; }

void bk__arena_free(void) {
    if (s_frame_arena.base != nullptr) {
        bk__free(s_frame_arena.base);
        s_frame_arena.base = nullptr;
        s_frame_arena.capacity = 0;
        s_frame_arena.used = 0;
    }
}

void *bk_frame_alloc(usize size, usize align) {
    if (s_frame_arena.base == nullptr) {
        unsigned char *base = bk__alloc(s_arena_default_capacity);
        BK_ASSERT(base != nullptr);
        if (base == nullptr) {
            return nullptr;
        }
        s_frame_arena.base = base;
        s_frame_arena.capacity = s_arena_default_capacity;
        s_frame_arena.used = 0;
    }

    if (align == 0) {
        align = alignof(max_align_t);
    } else {
        BK_ASSERT((align & (align - 1)) == 0);
    }

    usize worst_case = s_frame_arena.used + (align - 1) + size;
    if (worst_case > s_frame_arena.capacity) {
        usize new_capacity = s_frame_arena.capacity;
        while (new_capacity < worst_case) {
            new_capacity *= 2;
        }
        unsigned char *new_base = bk__realloc(s_frame_arena.base, new_capacity);
        if (new_base == nullptr) {
            BK_ASSERT(false);
            return nullptr;
        }
        s_frame_arena.base = new_base;
        s_frame_arena.capacity = new_capacity;
        SDL_Log("BK: frame arena grew to %zu bytes", new_capacity);
    }

    uintptr_t base_addr = (uintptr_t)s_frame_arena.base;
    uintptr_t cursor_addr = base_addr + s_frame_arena.used;
    uintptr_t aligned_addr = (cursor_addr + (align - 1)) & ~(uintptr_t)(align - 1);
    usize aligned_offset = (usize)(aligned_addr - base_addr);

    s_frame_arena.used = aligned_offset + size;
    return s_frame_arena.base + aligned_offset;
}

#ifndef NDEBUG
static constexpr bool s_gpu_debug_mode = true;
#else
static constexpr bool s_gpu_debug_mode = false;
#endif

typedef struct BK_AppState {
    BK_AppDesc desc;
    BK_Clock clock;
    SDL_Window *window;
    SDL_GPUDevice *gpu;
    u64 boot_now_ns;
} BK_AppState;

static BK_AppState s_app;

static SDL_AppResult s_boot_fail(const char *msg) {
    SDL_Log("BK: %s", msg);
#ifdef NDEBUG
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bielik2D", msg, s_app.window);
#endif
    return SDL_APP_FAILURE;
}

SDL_AppResult bk__boot(BK_AppDesc (*get_desc)(void), void **appstate, int argc, char **argv) {
    s_app.desc = get_desc();

    if (s_app.desc.window.title == nullptr) {
        s_app.desc.window.title = "Bielik2D";
    }
    if (s_app.desc.window.width == 0) {
        s_app.desc.window.width = 1280;
    }
    if (s_app.desc.window.height == 0) {
        s_app.desc.window.height = 720;
    }
    if (s_app.desc.time.max_ticks_per_frame == 0) {
        s_app.desc.time.max_ticks_per_frame = 8;
    }
    if (s_app.desc.time.max_frame_dt == 0.0) {
        s_app.desc.time.max_frame_dt = 0.25;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        char msg[256];
        SDL_snprintf(msg, sizeof msg, "SDL_Init failed: %s", SDL_GetError());
        return s_boot_fail(msg);
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_HIDDEN;
    if (s_app.desc.window.resizable) {
        window_flags |= SDL_WINDOW_RESIZABLE;
    }
    if (s_app.desc.window.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }
    s_app.window = SDL_CreateWindow(s_app.desc.window.title, s_app.desc.window.width,
                                    s_app.desc.window.height, window_flags);
    if (!s_app.window) {
        char msg[256];
        SDL_snprintf(msg, sizeof msg, "SDL_CreateWindow failed: %s", SDL_GetError());
        return s_boot_fail(msg);
    }

    s_app.gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                        SDL_GPU_SHADERFORMAT_MSL,
                                    s_gpu_debug_mode, nullptr);
    if (!s_app.gpu) {
        char msg[256];
        SDL_snprintf(msg, sizeof msg, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return s_boot_fail(msg);
    }

    if (!SDL_ClaimWindowForGPUDevice(s_app.gpu, s_app.window)) {
        char msg[256];
        SDL_snprintf(msg, sizeof msg, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return s_boot_fail(msg);
    }

    SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!s_app.desc.window.vsync &&
        SDL_WindowSupportsGPUPresentMode(s_app.gpu, s_app.window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    if (!SDL_SetGPUSwapchainParameters(s_app.gpu, s_app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       present_mode)) {
        char msg[256];
        SDL_snprintf(msg, sizeof msg, "SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
        return s_boot_fail(msg);
    }

    SDL_ShowWindow(s_app.window);

    s_app.boot_now_ns = SDL_GetTicksNS();
    bk_clock_init(&s_app.clock, s_app.desc.time.tick_hz, s_app.desc.time.max_ticks_per_frame,
                  s_app.desc.time.max_frame_dt, s_app.boot_now_ns);

    bk__task_set_desc(&s_app.desc.tasks);

    *appstate = s_app.desc.userdata;
    BK_Result result = BK_CONTINUE;
    if (s_app.desc.init) {
        result = s_app.desc.init(appstate, argc, argv);
    }
    if (result == BK_DONE) {
        return (SDL_AppResult)BK_DONE;
    }
    if (result != BK_CONTINUE) {
        return s_boot_fail("app init callback failed");
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult bk__iterate(void *appstate) {
    u64 now = SDL_GetTicksNS();
    BK_ClockFrame cf = bk_clock_advance(&s_app.clock, now);

    BK_FrameInfo info = {0};
    for (int i = 0; i < cf.ticks; i++) {
        // TODO(phase4): input snapshot slot — bk_input__begin_tick() runs here
        u64 tick_i = s_app.clock.tick - (u64)cf.ticks + (u64)i + 1;
        info = (BK_FrameInfo){
            .tick = tick_i,
            .sim_time = (f64)tick_i * bk_clock_fixed_dt(&s_app.clock),
            .real_time = (f64)(now - s_app.boot_now_ns) / 1e9,
            .dt = s_app.desc.time.tick_hz != 0 ? bk_clock_fixed_dt(&s_app.clock) : cf.frame_dt,
            .alpha = 0.0,
        };

        if (s_app.desc.update) {
            BK_Result result = s_app.desc.update(appstate, &info);
            if (result != BK_CONTINUE) {
                return (SDL_AppResult)result;
            }
        }
        // TODO(phase8): physics slot — bk_physics__step() runs here iff bk_physics_init was called
        if (s_app.desc.post_update) {
            BK_Result result = s_app.desc.post_update(appstate, &info);
            if (result != BK_CONTINUE) {
                return (SDL_AppResult)result;
            }
        }
    }

    info.tick = s_app.clock.tick;
    info.sim_time = bk_clock_sim_time(&s_app.clock);
    info.real_time = (f64)(now - s_app.boot_now_ns) / 1e9;
    info.dt = cf.frame_dt;
    info.alpha = cf.alpha;

    if (s_app.desc.render) {
        s_app.desc.render(appstate, &info);
    }
    bk__gfx_flush();
    bk__arena_reset();

    return SDL_APP_CONTINUE;
}

SDL_AppResult bk__event(void *appstate, SDL_Event *event) {
    if (s_app.desc.event) {
        BK_Result result = s_app.desc.event(appstate, event);
        return (SDL_AppResult)result;
    }
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void bk__shutdown(void *appstate, SDL_AppResult result) {
    if (s_app.desc.quit) {
        s_app.desc.quit(appstate, (BK_Result)result);
    }
    bk__arena_free();
    if (s_app.gpu && s_app.window) {
        SDL_ReleaseWindowFromGPUDevice(s_app.gpu, s_app.window);
    }
    if (s_app.gpu) {
        SDL_DestroyGPUDevice(s_app.gpu);
    }
    if (s_app.window) {
        SDL_DestroyWindow(s_app.window);
    }
}

SDL_Window *bk_window(void) { return s_app.window; }

SDL_GPUDevice *bk_gpu(void) { return s_app.gpu; }

void bk_quit(void) {
    SDL_Event event = {.type = SDL_EVENT_QUIT};
    SDL_PushEvent(&event);
}

static BK_AppDesc s_run_desc;

static BK_AppDesc s_run_get_desc(void) { return s_run_desc; }

static SDL_AppResult s_run_init(void **appstate, int argc, char **argv) {
    return bk__boot(s_run_get_desc, appstate, argc, argv);
}

static SDL_AppResult s_run_iterate(void *appstate) { return bk__iterate(appstate); }

static SDL_AppResult s_run_event(void *appstate, SDL_Event *event) {
    return bk__event(appstate, event);
}

static void s_run_quit(void *appstate, SDL_AppResult result) { bk__shutdown(appstate, result); }

int bk_run(const BK_AppDesc *desc, int argc, char **argv) {
    BK_ASSERT(desc != nullptr);

    s_run_desc = *desc;
    return SDL_EnterAppMainCallbacks(argc, argv, s_run_init, s_run_iterate, s_run_event,
                                     s_run_quit);
}
```

(`uintptr_t` is untouched — it's not one of the 13 types in `bk_types.h`, per spec §8's "no new types beyond the 13 listed." `%zu` in the `SDL_Log` format string is still correct: `usize` *is* `size_t`.)

- [ ] **Step 4: Migrate `tests/test_app_lifecycle.c`**

Replace the whole file:

```c
#include "bk_test.h"
#include <bielik/bk_app.h>
#include <stdio.h>

static int s_update_calls = 0;

static BK_Result test_init(void **state, int argc, char **argv) {
    (void)state;
    (void)argc;
    (void)argv;
    REQUIRE(bk_window() != nullptr);
    REQUIRE(bk_gpu() != nullptr);
    return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *frame) {
    (void)state;
    s_update_calls++;
    REQUIRE_EQ_U64(frame->tick, (u64)s_update_calls);
    REQUIRE_NEAR(frame->dt, 1.0 / 60.0, 1e-9);
    REQUIRE(frame->alpha == 0.0);
    REQUIRE(frame->real_time >= 0.0);
    if (s_update_calls >= 3) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *frame) {
    (void)state;
    (void)frame;
}

int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .window = {.title = "test_app_lifecycle", .width = 64, .height = 64},
        .time = {.tick_hz = 60},
        .init = test_init,
        .update = test_update,
        .render = test_render,
    };
    int result = bk_run(&desc, argc, argv);
    REQUIRE(result == 0);
    REQUIRE(s_update_calls == 3);
    printf("test_app_lifecycle: OK\n");
    return 0;
}
```

- [ ] **Step 5: Build with `-DBK_WERROR=ON`**

Run: `cmake --build build`
Expected: clean, zero warnings. This is the widest-blast-radius file in the plan (every other module includes `bk_app.h`) — a failure here means something downstream hasn't been re-checked; don't proceed to Task 3 until this is clean.

- [ ] **Step 6: Run the affected tests**

Run: `ctest --test-dir build --output-on-failure -R 'test_version|test_app_lifecycle|test_header_bk_app|test_header_bk_main'`
Expected: all pass. (Everything else — `bk_task`, `bk_gfx*`, samples — still has its *old* uint32_t/single-letter code at this point and won't build yet; that's fine, later tasks fix it. If the whole-project build fails here because of downstream files that haven't been migrated yet, build only the targets this task's tests need: `cmake --build build --target test_version test_app_lifecycle test_header_bk_app test_header_bk_main` instead of a full build, and note in the task's outcome that the rest of the tree is mid-migration.)

- [ ] **Step 7: Commit**

```bash
git add include/bielik/bk_app.h src/internal/bk_app_internal.h src/bk_app.c tests/test_app_lifecycle.c
git commit -m "$(cat <<'EOF'
migrate bk_app to bk_types.h and rename its single-letter identifiers

BK_FrameInfo *f -> *frame, SDL_Event *e -> *event, BK_Result r ->
result, and BK_WindowDesc's w/h -> width/height (never actually
spelled out despite the design draft's claim otherwise).
EOF
)"
```

---

## Task 3: `bk_task` migration

**Files:**
- Modify: `include/bielik/bk_task.h`
- Modify: `src/bk_task.c`
- Modify: `tests/test_task.c`

**Interfaces:**
- Consumes: `BK_TaskFn`, `BK_TaskSystemDesc` from Task 2's `bk_app.h` (`i32 start/end/count/min_range`, `u32 worker_index`).
- Produces: `void bk_task_run(BK_TaskFn fn, i32 count, i32 min_range, void *arg)` — no other task calls this.

- [ ] **Step 1: Migrate `include/bielik/bk_task.h`**

Replace the whole file (adds its own explicit `bk_types.h` include, rather than relying on `bk_app.h` transitively bringing in `i32` — this is what keeps Task 3 independent of Task 2's landing order, matching the Global Constraints note below):

```c
#pragma once
#include <bielik/bk_app.h>
#include <bielik/bk_types.h>

/// Runs fn over [0,count) honoring the app's task system (serial in v1).
/// Blocks until complete. min_range is a splitting hint for real schedulers.
void bk_task_run(BK_TaskFn fn, i32 count, i32 min_range, void *arg);
```

- [ ] **Step 2: Migrate `src/bk_task.c`**

Replace the whole file:

```c
#include "internal/bk_task_internal.h"
#include <bielik/bk_task.h>
#include <stddef.h>

static BK_TaskSystemDesc s_desc;

void bk__task_set_desc(const BK_TaskSystemDesc *desc) {
    s_desc = desc ? *desc : (BK_TaskSystemDesc){0};
}

void bk_task_run(BK_TaskFn fn, i32 count, i32 min_range, void *arg) {
    BK_ASSERT(fn != nullptr);

    if (count <= 0) {
        return;
    }

    if (s_desc.enqueue == nullptr) {
        fn(0, count, 0, arg);
        return;
    }

    void *task = s_desc.enqueue(fn, count, min_range, arg, s_desc.ctx);
    if (s_desc.finish != nullptr) {
        s_desc.finish(task, s_desc.ctx);
    }
}
```

(`<stddef.h>` was already unused before this migration — left in place; removing unrelated includes is out of scope for this sweep.)

`src/internal/bk_task_internal.h` needs no changes: it only references `BK_TaskSystemDesc *`, whose fields already migrated in Task 2.

- [ ] **Step 3: Migrate `tests/test_task.c`**

Replace the whole file:

```c
#include "bk_test.h"
#include "internal/bk_task_internal.h"
#include <bielik/bk_task.h>

typedef struct RangeSpy {
    int buf[64];
    int call_count;
    i32 last_start;
    i32 last_end;
} RangeSpy;

static void range_fn(i32 start, i32 end, u32 worker_index, void *arg) {
    RangeSpy *spy = (RangeSpy *)arg;
    spy->call_count++;
    spy->last_start = start;
    spy->last_end = end;
    REQUIRE(worker_index == 0);
    for (i32 i = start; i < end; i++) {
        spy->buf[i]++;
    }
}

static void test_serial_executor_covers_full_range_once(void) {
    bk__task_set_desc(nullptr);

    RangeSpy spy = {0};
    const i32 count = 32;
    bk_task_run(range_fn, count, 4, &spy);

    REQUIRE(spy.call_count == 1);
    REQUIRE(spy.last_start == 0);
    REQUIRE(spy.last_end == count);
    for (i32 i = 0; i < count; i++) {
        REQUIRE(spy.buf[i] == 1);
    }
    for (i32 i = count; i < 64; i++) {
        REQUIRE(spy.buf[i] == 0);
    }
}

static void counting_fn(i32 start, i32 end, u32 worker_index, void *arg) {
    (void)start;
    (void)end;
    (void)worker_index;
    int *call_count = (int *)arg;
    (*call_count)++;
}

static void test_zero_and_negative_count_call_nothing(void) {
    bk__task_set_desc(nullptr);

    int call_count = 0;
    bk_task_run(counting_fn, 0, 1, &call_count);
    bk_task_run(counting_fn, -5, 1, &call_count);
    REQUIRE(call_count == 0);
}

typedef struct EnqueueCall {
    BK_TaskFn fn;
    i32 count;
    i32 min_range;
    void *arg;
    void *ctx;
    int calls;
} EnqueueCall;

typedef struct FinishCall {
    void *task;
    void *ctx;
    int calls;
} FinishCall;

typedef struct CustomTaskSystemSpy {
    EnqueueCall enqueue;
    FinishCall finish;
} CustomTaskSystemSpy;

static int s_task_token;

static void *fake_enqueue(BK_TaskFn fn, i32 count, i32 min_range, void *arg, void *ctx) {
    CustomTaskSystemSpy *spy = (CustomTaskSystemSpy *)ctx;
    spy->enqueue.fn = fn;
    spy->enqueue.count = count;
    spy->enqueue.min_range = min_range;
    spy->enqueue.arg = arg;
    spy->enqueue.ctx = ctx;
    spy->enqueue.calls++;
    return &s_task_token;
}

static void fake_finish(void *task, void *ctx) {
    CustomTaskSystemSpy *spy = (CustomTaskSystemSpy *)ctx;
    spy->finish.task = task;
    spy->finish.ctx = ctx;
    spy->finish.calls++;
}

static void unused_fn(i32 start, i32 end, u32 worker_index, void *arg) {
    (void)start;
    (void)end;
    (void)worker_index;
    (void)arg;
}

static void test_custom_desc_pass_through(void) {
    CustomTaskSystemSpy spy = {0};
    BK_TaskSystemDesc desc = {
        .ctx = &spy,
        .enqueue = fake_enqueue,
        .finish = fake_finish,
    };
    bk__task_set_desc(&desc);

    int dummy_arg = 0;
    bk_task_run(unused_fn, 10, 3, &dummy_arg);

    REQUIRE(spy.enqueue.calls == 1);
    REQUIRE(spy.enqueue.fn == unused_fn);
    REQUIRE(spy.enqueue.count == 10);
    REQUIRE(spy.enqueue.min_range == 3);
    REQUIRE(spy.enqueue.arg == &dummy_arg);
    REQUIRE(spy.enqueue.ctx == &spy);

    REQUIRE(spy.finish.calls == 1);
    REQUIRE(spy.finish.task == &s_task_token);
    REQUIRE(spy.finish.ctx == &spy);

    bk__task_set_desc(nullptr);
}

static void test_custom_desc_without_finish_and_zero_count(void) {
    CustomTaskSystemSpy spy = {0};
    BK_TaskSystemDesc desc = {
        .ctx = &spy,
        .enqueue = fake_enqueue,
    };
    bk__task_set_desc(&desc);

    bk_task_run(unused_fn, 0, 1, &spy);
    REQUIRE(spy.enqueue.calls == 0);

    bk_task_run(unused_fn, 10, 3, &spy);
    REQUIRE(spy.enqueue.calls == 1);
    REQUIRE(spy.finish.calls == 0);

    bk__task_set_desc(nullptr);
}

int main(void) {
    test_serial_executor_covers_full_range_once();
    test_zero_and_negative_count_call_nothing();
    test_custom_desc_pass_through();
    test_custom_desc_without_finish_and_zero_count();
    printf("test_task: OK\n");
    return 0;
}
```

- [ ] **Step 4: Build and test**

Run: `cmake --build build --target test_task test_header_bk_task`
Run: `ctest --test-dir build --output-on-failure -R 'test_task|test_header_bk_task'`
Expected: clean build, zero warnings, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/bielik/bk_task.h src/bk_task.c tests/test_task.c
git commit -m "migrate bk_task to bk_types.h"
```

---

## Task 4: `bk_gfx` migration (core + capture)

Covers the module owning the swapchain flush and frame-capture feature: `bk_gfx.h`/`.c`, its internal header, and both test files that exercise it. Includes the newly-found `BK_Color c` single-letter rename (`bk_gfx.c`'s `bk__gfx_flush` and all three color-reading tests in `test_gfx.c`).

**Files:**
- Modify: `include/bielik/bk_gfx.h`
- Modify: `src/internal/bk_gfx_internal.h`
- Modify: `src/bk_gfx.c`
- Modify: `tests/test_gfx.c`
- Modify: `tests/test_gfx_capture.c`

**Interfaces:**
- Consumes: `bk_gpu()`, `bk_window()`, `bk__alloc`/`bk__free` from Task 2.
- Produces: `void bk_gfx_draw(i32 vertex_count)`, `void bk_gfx_draw_indexed(i32 index_count)`, `BK_Color { f32 r, g, b, a; }` — Task 5–7's samples call these.
- Produces (internal, test-only): `i32 bk__gfx_get_pending_vertex_count(void)`, `i32 bk__gfx_get_pending_index_count(void)`.
- `void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *texture, Uint32 width, Uint32 height, SDL_GPUTextureFormat format)` keeps `Uint32` — direct SDL_GPU call-boundary type, exempt per spec §5/§7. Task 5's `test_gfx_pipeline.c`/`test_gfx_compute.c` call this exact signature.

- [ ] **Step 1: Migrate `include/bielik/bk_gfx.h`**

Replace the whole file:

```c
#pragma once
#include <bielik/bk_types.h>

typedef struct BK_GfxPipeline BK_GfxPipeline;
typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;
typedef struct BK_GfxSampler BK_GfxSampler;

/// RGBA color.
typedef struct BK_Color {
    f32 r, g, b, a;
} BK_Color;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);

/// Binds a pipeline to be used by the next bk_gfx_draw/bk_gfx_draw_indexed call this
/// frame. The binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Issues a draw of vertex_count vertices using the most recently bound pipeline.
/// Must be called after bk_gfx_bind_pipeline in the same frame.
void bk_gfx_draw(i32 vertex_count);

/// Binds a vertex buffer to slot 0 for the next draw call this frame. The binding is
/// consumed (cleared) by the frame's flush.
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);

/// Binds an index buffer for the next bk_gfx_draw_indexed call this frame. Indices
/// are always read as 16-bit (see bk_gfx_buffer.h's BK_GFX_BUFFER_USAGE_INDEX). The
/// binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);

/// Binds a texture and sampler pair to fragment slot 0 for the next draw call this
/// frame. The binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);

/// Issues an indexed draw of index_count indices using the most recently bound
/// pipeline, vertex buffer, and index buffer. Must be called after
/// bk_gfx_bind_pipeline/bk_gfx_bind_vertex_buffer/bk_gfx_bind_index_buffer in the
/// same frame.
void bk_gfx_draw_indexed(i32 index_count);

/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again
/// each frame you want captured. Failures (bad path, unsupported swapchain
/// composition) are logged via SDL_Log with a "BK: " prefix, not returned -- the
/// actual capture happens later, inside the frame's flush.
void bk_gfx_request_capture(const char *path);
```

- [ ] **Step 2: Migrate `src/internal/bk_gfx_internal.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>

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
i32 bk__gfx_get_pending_vertex_count(void);

/// Test-only accessor: returns the buffer bound via bk_gfx_bind_vertex_buffer this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxBuffer *bk__gfx_get_pending_vertex_buffer(void);

/// Test-only accessor: returns the buffer bound via bk_gfx_bind_index_buffer this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxBuffer *bk__gfx_get_pending_index_buffer(void);

/// Test-only accessor: returns the texture bound via bk_gfx_bind_texture this frame,
/// or nullptr if none has been bound since the last flush.
BK_GfxTexture *bk__gfx_get_pending_texture(void);

/// Test-only accessor: returns the sampler bound via bk_gfx_bind_texture this frame,
/// or nullptr if none has been bound since the last flush.
BK_GfxSampler *bk__gfx_get_pending_sampler(void);

/// Test-only accessor: returns the index count set via bk_gfx_draw_indexed this
/// frame, or 0 if bk_gfx_draw_indexed hasn't been called since the last flush.
i32 bk__gfx_get_pending_index_count(void);

/// Test-only accessor: returns the path set via bk_gfx_request_capture this frame, or
/// an empty string if none has been requested since the last flush.
const char *bk__gfx_get_pending_capture_path(void);

/// Downloads width*height pixels (4 bytes/pixel) from texture via a copy pass added
/// to cmd, then submits cmd and waits for the GPU fence. cmd must not have been
/// submitted yet, and must not be used for anything else afterward -- this call
/// submits it on every path, success or failure. format must be
/// SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM or SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM (the
/// only 4-byte-per-pixel formats this helper supports; enforced by assertion, since
/// which format a caller passes is a programmer decision, not external data).
/// Returns a heap-allocated copy of the pixels (release with bk__free), or nullptr on
/// failure (logs via SDL_Log with a "BK: " prefix).
void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format);
```

(`Uint32 width, Uint32 height` stay — direct SDL_GPU call-boundary type, see spec §5/§7's exemption list.)

- [ ] **Step 3: Migrate `src/bk_gfx.c`**

Replace the whole file:

```c
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>

static BK_Color s_clear_color = {0.1f, 0.1f, 0.12f, 1.0f};

void bk_gfx_set_clear_color(BK_Color color) { s_clear_color = color; }

BK_Color bk__gfx_get_clear_color(void) { return s_clear_color; }

static BK_GfxPipeline *s_pending_pipeline = nullptr;
static i32 s_pending_vertex_count = 0;

void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline) {
    BK_ASSERT(pipeline != nullptr);
    s_pending_pipeline = pipeline;
}

void bk_gfx_draw(i32 vertex_count) {
    BK_ASSERT(vertex_count > 0);
    s_pending_vertex_count = vertex_count;
}

BK_GfxPipeline *bk__gfx_get_pending_pipeline(void) { return s_pending_pipeline; }

i32 bk__gfx_get_pending_vertex_count(void) { return s_pending_vertex_count; }

static BK_GfxBuffer *s_pending_vertex_buffer = nullptr;
static BK_GfxBuffer *s_pending_index_buffer = nullptr;
static BK_GfxTexture *s_pending_texture = nullptr;
static BK_GfxSampler *s_pending_sampler = nullptr;
static i32 s_pending_index_count = 0;

void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    s_pending_vertex_buffer = buffer;
}

void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    s_pending_index_buffer = buffer;
}

void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler) {
    BK_ASSERT(texture != nullptr);
    BK_ASSERT(sampler != nullptr);
    s_pending_texture = texture;
    s_pending_sampler = sampler;
}

void bk_gfx_draw_indexed(i32 index_count) {
    BK_ASSERT(index_count > 0);
    s_pending_index_count = index_count;
}

BK_GfxBuffer *bk__gfx_get_pending_vertex_buffer(void) { return s_pending_vertex_buffer; }

BK_GfxBuffer *bk__gfx_get_pending_index_buffer(void) { return s_pending_index_buffer; }

BK_GfxTexture *bk__gfx_get_pending_texture(void) { return s_pending_texture; }

BK_GfxSampler *bk__gfx_get_pending_sampler(void) { return s_pending_sampler; }

i32 bk__gfx_get_pending_index_count(void) { return s_pending_index_count; }

static char s_pending_capture_path[512];

void bk_gfx_request_capture(const char *path) {
    BK_ASSERT(path != nullptr);
    SDL_snprintf(s_pending_capture_path, sizeof s_pending_capture_path, "%s", path);
}

const char *bk__gfx_get_pending_capture_path(void) { return s_pending_capture_path; }

static SDL_PixelFormat s_pixel_format_for_gpu_format(SDL_GPUTextureFormat format) {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return SDL_PIXELFORMAT_RGBA32;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return SDL_PIXELFORMAT_BGRA32;
    default:
        return SDL_PIXELFORMAT_UNKNOWN;
    }
}

void bk__gfx_flush(void) {
    static bool s_logged_acquire_failure = false;

    BK_GfxPipeline *pending_pipeline = s_pending_pipeline;
    i32 pending_vertex_count = s_pending_vertex_count;
    BK_GfxBuffer *pending_vertex_buffer = s_pending_vertex_buffer;
    BK_GfxBuffer *pending_index_buffer = s_pending_index_buffer;
    BK_GfxTexture *pending_texture = s_pending_texture;
    BK_GfxSampler *pending_sampler = s_pending_sampler;
    i32 pending_index_count = s_pending_index_count;
    char pending_capture_path[sizeof s_pending_capture_path];
    SDL_memcpy(pending_capture_path, s_pending_capture_path, sizeof pending_capture_path);
    s_pending_pipeline = nullptr;
    s_pending_vertex_count = 0;
    s_pending_vertex_buffer = nullptr;
    s_pending_index_buffer = nullptr;
    s_pending_texture = nullptr;
    s_pending_sampler = nullptr;
    s_pending_index_count = 0;
    s_pending_capture_path[0] = '\0';

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
    if (!cmd) {
        if (!s_logged_acquire_failure) {
            SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            s_logged_acquire_failure = true;
        }
        return;
    }

    Uint32 swap_w = 0, swap_h = 0;
    SDL_GPUTexture *tex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, &swap_w, &swap_h)) {
        SDL_Log("BK: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    if (!tex) {
        // minimized/occluded — nothing to draw into this frame
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    BK_Color clear_color = bk__gfx_get_clear_color();
    SDL_GPUColorTargetInfo target = {
        .texture = tex,
        .clear_color = {clear_color.r, clear_color.g, clear_color.b, clear_color.a},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pending_pipeline));
        if (pending_vertex_buffer != nullptr) {
            SDL_GPUBufferBinding vertex_binding = {
                .buffer = bk__gfx_buffer_handle(pending_vertex_buffer)};
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        }
        if (pending_index_buffer != nullptr) {
            SDL_GPUBufferBinding index_binding = {.buffer =
                                                      bk__gfx_buffer_handle(pending_index_buffer)};
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        }
        if (pending_texture != nullptr && pending_sampler != nullptr) {
            SDL_GPUTextureSamplerBinding sampler_binding = {
                .texture = bk__gfx_texture_handle(pending_texture),
                .sampler = bk__gfx_sampler_handle(pending_sampler)};
            SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);
        }
        if (pending_vertex_count > 0) {
            SDL_DrawGPUPrimitives(pass, (Uint32)pending_vertex_count, 1, 0, 0);
        }
        if (pending_index_count > 0) {
            SDL_DrawGPUIndexedPrimitives(pass, (Uint32)pending_index_count, 1, 0, 0, 0);
        }
    }
    SDL_EndGPURenderPass(pass);

    if (pending_capture_path[0] != '\0') {
        SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
        SDL_PixelFormat sdl_format = s_pixel_format_for_gpu_format(format);
        if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
            SDL_Log("BK: bk_gfx_request_capture: unsupported swapchain format");
            SDL_SubmitGPUCommandBuffer(cmd);
        } else {
            void *pixels = bk__gfx_download_texture(bk_gpu(), cmd, tex, swap_w, swap_h, format);
            if (pixels != nullptr) {
                SDL_Surface *surface = SDL_CreateSurfaceFrom((int)swap_w, (int)swap_h, sdl_format,
                                                             pixels, (int)swap_w * 4);
                if (surface != nullptr) {
                    if (!SDL_SaveBMP(surface, pending_capture_path)) {
                        SDL_Log("BK: SDL_SaveBMP failed: %s", SDL_GetError());
                    }
                    SDL_DestroySurface(surface);
                } else {
                    SDL_Log("BK: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
                }
                bk__free(pixels);
            }
        }
    } else {
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}

void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format) {
    BK_ASSERT(device != nullptr);
    BK_ASSERT(cmd != nullptr);
    BK_ASSERT(texture != nullptr);
    BK_ASSERT(format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
              format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = width * height * 4,
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (transfer == nullptr) {
        SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return nullptr;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {.texture = texture, .w = width, .h = height, .d = 1};
    SDL_GPUTextureTransferInfo dst = {
        .transfer_buffer = transfer, .pixels_per_row = width, .rows_per_layer = height};
    SDL_DownloadFromGPUTexture(copy_pass, &src, &dst);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        SDL_Log("BK: SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
        SDL_Log("BK: SDL_WaitForGPUFences failed: %s", SDL_GetError());
        SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    SDL_ReleaseGPUFence(device, fence);

    void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }

    usize byte_size = (usize)width * (usize)height * 4;
    void *pixels = bk__alloc(byte_size);
    if (pixels != nullptr) {
        SDL_memcpy(pixels, mapped, byte_size);
    }

    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return pixels;
}
```

(`Uint32 swap_w, swap_h` and `bk__gfx_download_texture`'s `Uint32 width, Uint32 height` params stay — direct SDL_GPU boundary types. `(int)swap_w` casts stay — `SDL_CreateSurfaceFrom` itself takes `int` width/height.)

- [ ] **Step 4: Migrate `tests/test_gfx.c`**

Replace the whole file (every bare `BK_Color c` local renamed to `color`; no other changes — `dummy` isn't single-letter and is unrelated to this sweep):

```c
#include "bk_test.h"
#include "internal/bk_gfx_internal.h"
#include <bielik/bk_gfx.h>

static void test_default_clear_color(void) {
    BK_Color color = bk__gfx_get_clear_color();
    REQUIRE_NEAR(color.r, 0.1f, 1e-6);
    REQUIRE_NEAR(color.g, 0.1f, 1e-6);
    REQUIRE_NEAR(color.b, 0.12f, 1e-6);
    REQUIRE_NEAR(color.a, 1.0f, 1e-6);
}

static void test_set_then_get_round_trips(void) {
    bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});

    BK_Color color = bk__gfx_get_clear_color();
    REQUIRE_NEAR(color.r, 0.25f, 1e-6);
    REQUIRE_NEAR(color.g, 0.5f, 1e-6);
    REQUIRE_NEAR(color.b, 0.75f, 1e-6);
    REQUIRE_NEAR(color.a, 1.0f, 1e-6);
}

static void test_last_set_wins(void) {
    bk_gfx_set_clear_color((BK_Color){.r = 0.25f, .g = 0.5f, .b = 0.75f, .a = 1.0f});
    bk_gfx_set_clear_color((BK_Color){.r = 0.9f, .g = 0.8f, .b = 0.7f, .a = 0.6f});

    BK_Color color = bk__gfx_get_clear_color();
    REQUIRE_NEAR(color.r, 0.9f, 1e-6);
    REQUIRE_NEAR(color.g, 0.8f, 1e-6);
    REQUIRE_NEAR(color.b, 0.7f, 1e-6);
    REQUIRE_NEAR(color.a, 0.6f, 1e-6);
}

static void test_request_capture_sets_pending_path(void) {
    bk_gfx_request_capture("screenshot.bmp");

    REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "screenshot.bmp") == 0);
}

static void test_bind_pipeline_and_draw_sets_pending_state(void) {
    static int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);

    REQUIRE(bk__gfx_get_pending_pipeline() == fake_pipeline);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 3);
}

static void test_flush_early_return_clears_pending_state(void) {
    int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;
    BK_GfxBuffer *fake_vertex_buffer = (BK_GfxBuffer *)&dummy;
    BK_GfxBuffer *fake_index_buffer = (BK_GfxBuffer *)&dummy;
    BK_GfxTexture *fake_texture = (BK_GfxTexture *)&dummy;
    BK_GfxSampler *fake_sampler = (BK_GfxSampler *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);
    bk_gfx_bind_vertex_buffer(fake_vertex_buffer);
    bk_gfx_bind_index_buffer(fake_index_buffer);
    bk_gfx_bind_texture(fake_texture, fake_sampler);
    bk_gfx_draw_indexed(6);
    bk_gfx_request_capture("unreachable.bmp");

    // No app has been booted in this test binary, so bk_gpu() returns nullptr and
    // SDL_AcquireGPUCommandBuffer fails immediately -- this exercises bk__gfx_flush's
    // early-return path (command-buffer acquire failure) without needing a real
    // window/GPU device, and proves pending state doesn't survive it.
    bk__gfx_flush();

    REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 0);
    REQUIRE(bk__gfx_get_pending_vertex_buffer() == nullptr);
    REQUIRE(bk__gfx_get_pending_index_buffer() == nullptr);
    REQUIRE(bk__gfx_get_pending_texture() == nullptr);
    REQUIRE(bk__gfx_get_pending_sampler() == nullptr);
    REQUIRE(bk__gfx_get_pending_index_count() == 0);
    REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "") == 0);
}

static void test_bind_buffers_texture_and_draw_indexed_sets_pending_state(void) {
    static int dummy;
    BK_GfxBuffer *fake_vertex_buffer = (BK_GfxBuffer *)&dummy;
    BK_GfxBuffer *fake_index_buffer = (BK_GfxBuffer *)&dummy;
    BK_GfxTexture *fake_texture = (BK_GfxTexture *)&dummy;
    BK_GfxSampler *fake_sampler = (BK_GfxSampler *)&dummy;

    bk_gfx_bind_vertex_buffer(fake_vertex_buffer);
    bk_gfx_bind_index_buffer(fake_index_buffer);
    bk_gfx_bind_texture(fake_texture, fake_sampler);
    bk_gfx_draw_indexed(6);

    REQUIRE(bk__gfx_get_pending_vertex_buffer() == fake_vertex_buffer);
    REQUIRE(bk__gfx_get_pending_index_buffer() == fake_index_buffer);
    REQUIRE(bk__gfx_get_pending_texture() == fake_texture);
    REQUIRE(bk__gfx_get_pending_sampler() == fake_sampler);
    REQUIRE(bk__gfx_get_pending_index_count() == 6);
}

int main(void) {
    test_default_clear_color();
    test_set_then_get_round_trips();
    test_last_set_wins();
    test_request_capture_sets_pending_path();
    test_bind_pipeline_and_draw_sets_pending_state();
    test_bind_buffers_texture_and_draw_indexed_sets_pending_state();
    test_flush_early_return_clears_pending_state();
    printf("test_gfx: OK\n");
    return 0;
}
```

- [ ] **Step 5: Migrate `tests/test_gfx_capture.c`**

Replace the whole file (`const BK_FrameInfo *f` → `*frame`, `.w = 64, .h = 64` → `.width = 64, .height = 64` on the `BK_WindowDesc` initializer only — `loaded->w`/`loaded->h`/`rgba->w`/`rgba->h`/`rgba->pitch` are SDL's own `SDL_Surface` fields and stay unchanged, `const uint8_t *pixels` → `const u8 *pixels`, `size_t center` → `usize center`):

```c
#include "bk_test.h"
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <stdio.h>
#include <stdlib.h>

static int s_update_calls = 0;
static bool s_capture_requested = false;
static char s_capture_path[512];

static BK_Result test_init(void **state, int argc, char **argv) {
    (void)state;
    (void)argc;
    (void)argv;
    REQUIRE(bk_window() != nullptr);
    REQUIRE(bk_gpu() != nullptr);

    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);
    SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_capture_output.bmp", base_path);
    SDL_RemovePath(s_capture_path); // in case a prior failed run left one behind
    return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *frame) {
    (void)state;
    (void)frame;
    s_update_calls++;
    if (s_update_calls >= 3) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *frame) {
    (void)state;
    (void)frame;
    bk_gfx_set_clear_color((BK_Color){.r = 0.2f, .g = 0.4f, .b = 0.6f, .a = 1.0f});
    // Request on the first render call reached, whichever tick that lands on --
    // robust to fixed-tick batching (multiple ticks can run before one render call).
    if (!s_capture_requested) {
        bk_gfx_request_capture(s_capture_path);
        s_capture_requested = true;
    }
}

int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .window = {.title = "test_gfx_capture", .width = 64, .height = 64},
        .time = {.tick_hz = 60},
        .init = test_init,
        .update = test_update,
        .render = test_render,
    };
    int result = bk_run(&desc, argc, argv);
    REQUIRE(result == 0);
    REQUIRE(s_capture_requested);

    SDL_Surface *loaded = SDL_LoadBMP(s_capture_path);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->w == 64);
    REQUIRE(loaded->h == 64);

    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(rgba != nullptr);
    SDL_DestroySurface(loaded);

    const u8 *pixels = (const u8 *)rgba->pixels;
    constexpr int tolerance = 5;
    usize center = ((usize)(rgba->h / 2) * (usize)rgba->pitch) + (usize)(rgba->w / 2) * 4;
    REQUIRE(abs((int)pixels[center + 0] - 51) <= tolerance);  // R = 0.2 * 255
    REQUIRE(abs((int)pixels[center + 1] - 102) <= tolerance); // G = 0.4 * 255
    REQUIRE(abs((int)pixels[center + 2] - 153) <= tolerance); // B = 0.6 * 255
    REQUIRE(abs((int)pixels[center + 3] - 255) <= tolerance); // A

    SDL_DestroySurface(rgba);
    SDL_RemovePath(s_capture_path);
    printf("test_gfx_capture: OK\n");
    return 0;
}
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build --target test_gfx test_gfx_capture test_header_bk_gfx`
Run: `ctest --test-dir build --output-on-failure -R 'test_gfx$|test_gfx_capture|test_header_bk_gfx'`
Expected: clean build, zero warnings, all pass.

- [ ] **Step 7: Commit**

```bash
git add include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c \
        tests/test_gfx.c tests/test_gfx_capture.c
git commit -m "$(cat <<'EOF'
migrate bk_gfx to bk_types.h and rename its single-letter identifiers

BK_Color c -> color in bk__gfx_flush and every test_gfx.c color
assertion; BK_FrameInfo *f -> *frame and the window desc's w/h ->
width/height in test_gfx_capture.c.
EOF
)"
```

---

## Task 5: `bk_gfx_pipeline` migration (graphics + compute)

Covers `bk_gfx_pipeline.h`/`.c` (which implements both the graphics-pipeline API and the compute-pipeline/dispatch API in one file) and both test files that exercise it.

**Files:**
- Modify: `include/bielik/bk_gfx_pipeline.h`
- Modify: `tests/test_gfx_pipeline.c`
- Modify: `tests/test_gfx_compute.c`
- No changes: `src/bk_gfx_pipeline.c`, `src/internal/bk_gfx_pipeline_internal.h` (see Step 1 note — every primitive value in this `.c` flows through already-migrated header/struct fields or SDL boundary casts that stay syntactically identical regardless of the source type)

**Interfaces:**
- Consumes: `bk__gfx_download_texture` (Task 4), `bk__free`/`bk__alloc` (Task 2).
- Produces: `BK_GfxShaderVariant { const void *code; usize code_size; const char *entry_point; }`, `BK_GfxPipelineDesc`/`BK_GfxComputePipelineDesc`/`BK_GfxComputeDispatchDesc` with `i32` counts and `u32` vertex/threadgroup fields — Task 9's samples (`03_triangle`, `04_textured_quad`, `05_compute`) build these descs.

- [ ] **Step 1: Migrate `include/bielik/bk_gfx_pipeline.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_types.h>

/// One precompiled shader bytecode blob for a single backend format.
typedef struct BK_GfxShaderVariant {
    const void *code;
    usize code_size;
    const char *entry_point;
} BK_GfxShaderVariant;

/// One shader stage, precompiled to all three backend formats; bk_gfx_pipeline_create
/// selects the variant matching the device's supported shader formats
/// (SDL_GetGPUShaderFormats). Resource counts must match what the shader binary
/// declares (SDL_GPU validates this at creation). A variant with code == nullptr is
/// treated as unavailable.
typedef struct BK_GfxShaderDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    i32 num_samplers;
    i32 num_uniform_buffers;
} BK_GfxShaderDesc;

/// Per-vertex attribute formats supported by pipeline vertex input state.
typedef enum BK_GfxVertexFormat {
    BK_GFX_VERTEX_FORMAT_FLOAT2,
    BK_GFX_VERTEX_FORMAT_FLOAT3,
    BK_GFX_VERTEX_FORMAT_FLOAT4,
    BK_GFX_VERTEX_FORMAT_UBYTE4_NORM, // packed color
} BK_GfxVertexFormat;

/// One vertex attribute: which shader input location it feeds, which vertex buffer
/// slot it reads from, its format, and its byte offset within that slot's stride.
typedef struct BK_GfxVertexAttribute {
    u32 location;
    u32 buffer_slot;
    BK_GfxVertexFormat format;
    u32 offset;
} BK_GfxVertexAttribute;

/// One vertex buffer slot's stride, in bytes.
typedef struct BK_GfxVertexBufferLayout {
    u32 slot;
    u32 pitch;
} BK_GfxVertexBufferLayout;

typedef enum BK_GfxPrimitiveType {
    BK_GFX_PRIMITIVE_TRIANGLE_LIST,
    BK_GFX_PRIMITIVE_TRIANGLE_STRIP,
    BK_GFX_PRIMITIVE_LINE_LIST,
} BK_GfxPrimitiveType;

/// Fixed-function blend state. Two modes cover 2D's needs; more get added when a
/// real use case needs SDL_GPU's full blend-factor/op matrix.
typedef enum BK_GfxBlendMode {
    BK_GFX_BLEND_NONE,
    BK_GFX_BLEND_ALPHA,
} BK_GfxBlendMode;

/// Opaque graphics pipeline: compiled shaders + fixed-function state, bound in a
/// render pass before a draw call. Owns no per-frame resources.
typedef struct BK_GfxPipeline BK_GfxPipeline;

typedef struct BK_GfxPipelineDesc {
    BK_GfxShaderDesc vertex_shader;
    BK_GfxShaderDesc fragment_shader;

    // nullptr/0 => no vertex input (e.g. a procedural triangle driven by
    // gl_VertexIndex/SV_VertexID with no bound vertex buffer). Max 8 buffers, 16
    // attributes.
    const BK_GfxVertexBufferLayout *vertex_buffers;
    i32 num_vertex_buffers;
    const BK_GfxVertexAttribute *vertex_attributes;
    i32 num_vertex_attributes;

    BK_GfxPrimitiveType primitive_type;

    // Caller supplies the target format explicitly -- SDL_GetGPUSwapchainTextureFormat
    // for on-screen rendering; an offscreen texture's own format for headless/canvas
    // use. No bk_ wrapper needed.
    SDL_GPUTextureFormat color_target_format;
    BK_GfxBlendMode blend_mode;
} BK_GfxPipelineDesc;

/// Creates a graphics pipeline against the given device. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on any SDL_GPU failure (bad bytecode, unsupported
/// format/resource combination) -- this is a runtime-data-dependent operation, not a
/// programmer-error precondition, so failure is a recoverable return, not an assert.
/// device is explicit (not the bk_gpu() singleton) so pipelines can be created and
/// tested without a running app or window.
BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc);

/// Destroys a pipeline. No-op if pipeline is nullptr.
void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline);

typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;

/// One compute shader, precompiled to all three backend formats, plus the resource
/// counts and threadgroup size the shader binary declares. threadcount_x/y/z must
/// match the shader source's local_size_{x,y,z} -- SDL_GPU validates this at creation.
typedef struct BK_GfxComputePipelineDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    i32 num_readonly_storage_buffers;
    i32 num_readwrite_storage_textures;
    u32 threadcount_x;
    u32 threadcount_y;
    u32 threadcount_z;
} BK_GfxComputePipelineDesc;

/// Opaque compute pipeline: a compiled compute shader, bound and dispatched via
/// bk_gfx_compute_dispatch. Owns no per-dispatch resources.
typedef struct BK_GfxComputePipeline BK_GfxComputePipeline;

/// Creates a compute pipeline against the given device. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on any SDL_GPU failure. device is explicit, same
/// rationale as bk_gfx_pipeline_create.
BK_GfxComputePipeline *bk_gfx_compute_pipeline_create(SDL_GPUDevice *device,
                                                      const BK_GfxComputePipelineDesc *desc);

/// Destroys a compute pipeline. No-op if pipeline is nullptr.
void bk_gfx_compute_pipeline_destroy(BK_GfxComputePipeline *pipeline);

/// Describes one dispatch: which pipeline, which resources are bound to it (array
/// order matches binding slot order), and the workgroup counts.
typedef struct BK_GfxComputeDispatchDesc {
    BK_GfxComputePipeline *pipeline;
    BK_GfxTexture *const *readwrite_textures;
    i32 num_readwrite_textures;
    BK_GfxBuffer *const *readonly_buffers;
    i32 num_readonly_buffers;
    u32 groups_x;
    u32 groups_y;
    u32 groups_z;
} BK_GfxComputeDispatchDesc;

/// Dispatches a compute pipeline synchronously: acquires its own command buffer,
/// records the dispatch, submits, and blocks on a GPU fence until it completes.
/// Intended for setup-time work (e.g. procedurally filling a texture once at init),
/// not a per-frame call -- unlike bk_gfx_draw, there is no pending-slot/flush
/// integration, since compute work has no natural once-per-frame cadence the way the
/// render pass does. Returns false and logs via SDL_Log on SDL_GPU failure.
bool bk_gfx_compute_dispatch(const BK_GfxComputeDispatchDesc *desc);
```

- [ ] **Step 2: Confirm `src/bk_gfx_pipeline.c` and `src/internal/bk_gfx_pipeline_internal.h` need no edits**

Read both files and verify: every primitive value in `bk_gfx_pipeline.c` is either read from an already-migrated `desc->` field (e.g. `desc->num_vertex_buffers`, now `i32`) or cast straight to an SDL boundary type (e.g. `(Uint32)desc->num_vertex_buffers`) — both stay byte-for-byte identical regardless of the source field's type. Loop counters (`for (int i = 0; ...)`) stay plain `int` per the loop-counter exemption. There are no bare `uint32_t`/`size_t`/`float` local declarations anywhere in this file. `bk_gfx_pipeline_internal.h` only references `const BK_GfxPipeline *`, untouched by this migration. This step is a verification read, not an edit — if it turns out something *was* missed, fix it here rather than assuming.

- [ ] **Step 3: Migrate `tests/test_gfx_pipeline.c`**

Replace the whole file (`size_t *out_size` → `usize *out_size`; the `s_check_pixel` helper's `uint8_t` params → `u8`, its `size_t i` → `usize i` — `width`/`x`/`y`/`tolerance`/`size` stay plain `int`, they're private test-helper locals, not public API):

```c
#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_pipeline.h>
#include <stdio.h>
#include <stdlib.h>

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    REQUIRE(data != nullptr);
    return data;
}

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

static void test_create_and_destroy_pipeline_succeeds(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };

    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &desc);
    REQUIRE(pipeline != nullptr);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_DestroyGPUDevice(device);
}

static void test_out_of_range_vertex_counts_return_null(void) {
    // Defensive, independent of test-function call order: see the identical call
    // in test_create_and_destroy_pipeline_succeeds above for why this is required.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    // The bounds check runs before any shader is created, so no real shader
    // bytecode is needed here: an out-of-range count must fail before
    // desc.vertex_shader/fragment_shader are ever touched.
    BK_GfxPipelineDesc desc = {
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };

    desc.num_vertex_buffers = 9;
    REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

    desc.num_vertex_buffers = -1;
    REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

    desc.num_vertex_buffers = 0;
    desc.num_vertex_attributes = 17;
    REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

    desc.num_vertex_attributes = -1;
    REQUIRE(bk_gfx_pipeline_create(device, &desc) == nullptr);

    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_pipeline_destroy(nullptr); }

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
    usize i = ((usize)y * (usize)width + (usize)x) * 4;
    REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
    REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
    REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
    REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

static void test_draw_produces_expected_pixels(void) {
    constexpr int size = 64;
    constexpr int tolerance = 5;

    // Defensive, independent of test-function call order: see the identical call
    // in test_create_and_destroy_pipeline_succeeds above for why this is required.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc pipeline_desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
    REQUIRE(pipeline != nullptr);

    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = size,
        .height = size,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    SDL_GPUTexture *offscreen = SDL_CreateGPUTexture(device, &texture_info);
    REQUIRE(offscreen != nullptr);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    REQUIRE(cmd != nullptr);

    SDL_GPUColorTargetInfo color_target = {
        .texture = offscreen,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pipeline));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    void *pixels_buf = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(pixels_buf != nullptr);
    const u8 *pixels = (const u8 *)pixels_buf;

    // Center: well inside the triangle (NDC bbox [-0.5,0.5] on both axes covers the
    // middle half of the viewport) -> solid red.
    s_check_pixel(pixels, size, size / 2, size / 2, 255, 0, 0, 255, tolerance);
    // Corners, inset by 1px: outside the triangle's bounding box under any backend's
    // NDC-to-pixel axis convention -> clear color (black).
    s_check_pixel(pixels, size, 1, 1, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, size - 2, 1, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, 1, size - 2, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, size - 2, size - 2, 0, 0, 0, 255, tolerance);

    bk__free(pixels_buf);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_ReleaseGPUTexture(device, offscreen);
    SDL_DestroyGPUDevice(device);
}

int main(void) {
    test_create_and_destroy_pipeline_succeeds();
    test_out_of_range_vertex_counts_return_null();
    test_draw_produces_expected_pixels();
    test_destroy_null_is_noop();
    printf("test_gfx_pipeline: OK\n");
    return 0;
}
```

- [ ] **Step 4: Migrate `tests/test_gfx_compute.c`**

Replace the whole file (same `s_load_shader_file`/`s_check_pixel` changes as Step 3; `float params[8]` → `f32 params[8]`):

```c
#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_GPUDevice *s_create_device(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);
    return device;
}

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    REQUIRE(data != nullptr);
    return data;
}

static BK_GfxComputePipelineDesc s_load_gradient_compute_desc(void) {
    BK_GfxComputePipelineDesc desc = {
        .num_readonly_storage_buffers = 1,
        .num_readwrite_storage_textures = 1,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
    };
    desc.spirv.code = s_load_shader_file("gradient.compute.spv", &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file("gradient.compute.msl", &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_compute_desc(BK_GfxComputePipelineDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

static void test_create_and_destroy_compute_pipeline_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxComputePipelineDesc desc = s_load_gradient_compute_desc();
    BK_GfxComputePipeline *pipeline = bk_gfx_compute_pipeline_create(device, &desc);
    REQUIRE(pipeline != nullptr);

    bk_gfx_compute_pipeline_destroy(pipeline);
    s_free_compute_desc(&desc);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_compute_pipeline_destroy(nullptr); }

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
    usize i = ((usize)y * (usize)width + (usize)x) * 4;
    REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
    REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
    REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
    REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

// Dispatches gradient.comp (reads a read-only storage buffer of {base_color, scale},
// writes color = base_color + scale * vec4(uv, 0, 0) to a read-write storage texture)
// and checks the result against hand-computed literal expected values -- proves
// buffer-as-storage-input, texture-as-storage-output, and the dispatch itself all
// work together. Readback reuses bk__gfx_download_texture; no new download plumbing.
static void test_dispatch_produces_expected_gradient(void) {
    constexpr int size = 16;
    constexpr int tolerance = 5;

    SDL_GPUDevice *device = s_create_device();

    BK_GfxComputePipelineDesc pipeline_desc = s_load_gradient_compute_desc();
    BK_GfxComputePipeline *pipeline = bk_gfx_compute_pipeline_create(device, &pipeline_desc);
    REQUIRE(pipeline != nullptr);

    // Params: base_color = (0.2, 0.3, 0.4, 1.0), scale = (0.5, 0.4, 0.0, 0.0). std140
    // layout: two vec4s, 16-byte aligned, no padding needed.
    f32 params[8] = {0.2f, 0.3f, 0.4f, 1.0f, 0.5f, 0.4f, 0.0f, 0.0f};
    BK_GfxBuffer *params_buffer =
        bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_STORAGE_READ, sizeof params);
    REQUIRE(params_buffer != nullptr);
    REQUIRE(bk_gfx_buffer_upload(params_buffer, params, 0, sizeof params));

    BK_GfxTexture *target =
        bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, size, size);
    REQUIRE(target != nullptr);

    BK_GfxBuffer *readonly_buffers[1] = {params_buffer};
    BK_GfxTexture *readwrite_textures[1] = {target};
    BK_GfxComputeDispatchDesc dispatch_desc = {
        .pipeline = pipeline,
        .readwrite_textures = readwrite_textures,
        .num_readwrite_textures = 1,
        .readonly_buffers = readonly_buffers,
        .num_readonly_buffers = 1,
        .groups_x = (size + 7) / 8,
        .groups_y = (size + 7) / 8,
        .groups_z = 1,
    };
    REQUIRE(bk_gfx_compute_dispatch(&dispatch_desc));

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    REQUIRE(cmd != nullptr);
    void *pixels_buf = bk__gfx_download_texture(device, cmd, bk__gfx_texture_handle(target), size,
                                                size, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(pixels_buf != nullptr);
    const u8 *pixels = (const u8 *)pixels_buf;

    // uv = coord / (size - 1). color = base_color + scale * vec4(uv, 0, 0).
    // (0,0): uv=(0,0) -> (0.2, 0.3, 0.4, 1.0) -> (51, 77, 102, 255).
    s_check_pixel(pixels, size, 0, 0, 51, 77, 102, 255, tolerance);
    // (15,15): uv=(1,1) -> (0.7, 0.7, 0.4, 1.0) -> (178, 178, 102, 255).
    s_check_pixel(pixels, size, size - 1, size - 1, 178, 178, 102, 255, tolerance);
    // (15,0): uv=(1,0) -> (0.7, 0.3, 0.4, 1.0) -> (178, 77, 102, 255).
    s_check_pixel(pixels, size, size - 1, 0, 178, 77, 102, 255, tolerance);

    bk__free(pixels_buf);

    bk_gfx_compute_pipeline_destroy(pipeline);
    bk_gfx_buffer_destroy(params_buffer);
    bk_gfx_texture_destroy(target);
    s_free_compute_desc(&pipeline_desc);
    SDL_DestroyGPUDevice(device);
}

int main(void) {
    test_create_and_destroy_compute_pipeline_succeeds();
    test_destroy_null_is_noop();
    test_dispatch_produces_expected_gradient();
    printf("test_gfx_compute: OK\n");
    return 0;
}
```

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target test_gfx_pipeline test_gfx_compute test_header_bk_gfx_pipeline`
Run: `ctest --test-dir build --output-on-failure -R 'test_gfx_pipeline|test_gfx_compute|test_header_bk_gfx_pipeline'`
Expected: clean build, zero warnings, all pass.

- [ ] **Step 6: Commit**

```bash
git add include/bielik/bk_gfx_pipeline.h tests/test_gfx_pipeline.c tests/test_gfx_compute.c
git commit -m "migrate bk_gfx_pipeline to bk_types.h"
```

---

## Task 6: `bk_gfx_buffer` migration

**Files:**
- Modify: `include/bielik/bk_gfx_buffer.h`
- Modify: `src/internal/bk_gfx_buffer_internal.h`
- Modify: `src/bk_gfx_buffer.c`
- Modify: `tests/test_gfx_buffer.c`

**Interfaces:**
- Consumes: `bk__alloc`/`bk__free` (Task 2).
- Produces: `BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage, u32 size)`, `bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, u32 offset, u32 size)` — Task 5's `test_gfx_compute.c` and Task 9's samples (`04_textured_quad`, `05_compute`) call these.

- [ ] **Step 1: Migrate `include/bielik/bk_gfx_buffer.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_types.h>

/// What a buffer is used for. Exclusive, not a bitmask -- SDL_GPU itself rejects a
/// buffer created with both VERTEX and INDEX usage, and nothing in 2D needs a buffer
/// that is more than one thing at once.
typedef enum BK_GfxBufferUsage {
    BK_GFX_BUFFER_USAGE_VERTEX,
    BK_GFX_BUFFER_USAGE_INDEX,        // 16-bit indices; bk_gfx_bind_index_buffer hardcodes
                                      // the element size, see bk_gfx.h
    BK_GFX_BUFFER_USAGE_STORAGE_READ, // read-only storage buffer in a compute shader
} BK_GfxBufferUsage;

/// A GPU buffer: vertex data, index data, or compute storage input. Owns its device
/// upload machinery internally; callers just create, upload, bind (via bk_gfx), and
/// destroy.
typedef struct BK_GfxBuffer BK_GfxBuffer;

/// Creates a buffer of the given usage and byte size. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on SDL_GPU failure. device is explicit (not the
/// bk_gpu() singleton), matching bk_gfx_pipeline_create's precedent -- testable with
/// no window or running app.
BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage, u32 size);

/// Uploads size bytes from data into buffer starting at byte offset offset. Returns
/// false and logs via SDL_Log ("BK: " prefix) if offset + size exceeds the buffer's
/// size -- a runtime-data-dependent failure, not a programmer-error precondition, so
/// it's a recoverable return rather than an assert. buffer and data must be non-null
/// (BK_ASSERT).
bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, u32 offset, u32 size);

/// Destroys a buffer. No-op if buffer is nullptr.
void bk_gfx_buffer_destroy(BK_GfxBuffer *buffer);
```

- [ ] **Step 2: Migrate `src/internal/bk_gfx_buffer_internal.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_buffer.h>

/// Returns the underlying SDL_GPU buffer handle. Framework-internal; used by bk_gfx's
/// frame flush to bind the buffer pending from bk_gfx_bind_vertex_buffer/
/// bk_gfx_bind_index_buffer (added in a later task), and by compute dispatch to bind
/// read-only storage buffers.
SDL_GPUBuffer *bk__gfx_buffer_handle(const BK_GfxBuffer *buffer);

/// Returns the byte size buffer was created with. Framework-internal; test-only
/// accessor.
u32 bk__gfx_buffer_size(const BK_GfxBuffer *buffer);
```

- [ ] **Step 3: Migrate `src/bk_gfx_buffer.c`**

Replace the whole file:

```c
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx_buffer.h>

struct BK_GfxBuffer {
    SDL_GPUDevice *device;
    SDL_GPUBuffer *handle;
    u32 size;
};

static SDL_GPUBufferUsageFlags s_buffer_usage_flags(BK_GfxBufferUsage usage) {
    switch (usage) {
    case BK_GFX_BUFFER_USAGE_VERTEX:
        return SDL_GPU_BUFFERUSAGE_VERTEX;
    case BK_GFX_BUFFER_USAGE_INDEX:
        return SDL_GPU_BUFFERUSAGE_INDEX;
    case BK_GFX_BUFFER_USAGE_STORAGE_READ:
        return SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    }
    BK_ASSERT(false);
    return 0;
}

BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage, u32 size) {
    BK_ASSERT(device != nullptr);

    SDL_GPUBufferCreateInfo info = {
        .usage = s_buffer_usage_flags(usage),
        .size = size,
    };
    SDL_GPUBuffer *handle = SDL_CreateGPUBuffer(device, &info);
    if (handle == nullptr) {
        SDL_Log("BK: SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        return nullptr;
    }

    BK_GfxBuffer *buffer = bk__alloc(sizeof(BK_GfxBuffer));
    if (buffer == nullptr) {
        SDL_ReleaseGPUBuffer(device, handle);
        return nullptr;
    }
    buffer->device = device;
    buffer->handle = handle;
    buffer->size = size;
    return buffer;
}

bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, u32 offset, u32 size) {
    BK_ASSERT(buffer != nullptr);
    BK_ASSERT(data != nullptr);

    if (offset > buffer->size || size > buffer->size - offset) {
        SDL_Log("BK: bk_gfx_buffer_upload: offset %u + size %u exceeds buffer size %u", offset,
                size, buffer->size);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = size,
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(buffer->device, &transfer_info);
    if (transfer == nullptr) {
        SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(buffer->device, transfer, false);
    if (mapped == nullptr) {
        SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
        return false;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(buffer->device, transfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(buffer->device);
    if (cmd == nullptr) {
        SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation source = {.transfer_buffer = transfer, .offset = 0};
    SDL_GPUBufferRegion destination = {.buffer = buffer->handle, .offset = offset, .size = size};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(buffer->device, transfer);
    return true;
}

void bk_gfx_buffer_destroy(BK_GfxBuffer *buffer) {
    if (buffer == nullptr) {
        return;
    }
    SDL_ReleaseGPUBuffer(buffer->device, buffer->handle);
    bk__free(buffer);
}

SDL_GPUBuffer *bk__gfx_buffer_handle(const BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    return buffer->handle;
}

u32 bk__gfx_buffer_size(const BK_GfxBuffer *buffer) {
    BK_ASSERT(buffer != nullptr);
    return buffer->size;
}
```

(`%u` format specifiers stay correct: `u32` *is* `uint32_t`.)

- [ ] **Step 4: Migrate `tests/test_gfx_buffer.c`**

Replace the whole file (`uint8_t data[...]` → `u8 data[...]`; `<stdint.h>` stays included — it's the one file needing `UINT32_MAX`, per spec §7's grep-gate exemption list):

```c
#include "bk_test.h"
#include "internal/bk_gfx_buffer_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_buffer.h>
#include <stdint.h>
#include <stdio.h>

static SDL_GPUDevice *s_create_device(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);
    return device;
}

static void test_create_each_usage_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *vertex = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 256);
    REQUIRE(vertex != nullptr);
    REQUIRE(bk__gfx_buffer_size(vertex) == 256);

    BK_GfxBuffer *index = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_INDEX, 128);
    REQUIRE(index != nullptr);

    BK_GfxBuffer *storage = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_STORAGE_READ, 64);
    REQUIRE(storage != nullptr);

    bk_gfx_buffer_destroy(vertex);
    bk_gfx_buffer_destroy(index);
    bk_gfx_buffer_destroy(storage);
    SDL_DestroyGPUDevice(device);
}

static void test_upload_in_bounds_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    u8 data[32];
    for (int i = 0; i < 32; i++) {
        data[i] = (u8)i;
    }

    REQUIRE(bk_gfx_buffer_upload(buffer, data, 0, sizeof data));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_upload_at_offset_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    u8 data[16] = {0};
    REQUIRE(bk_gfx_buffer_upload(buffer, data, 32, sizeof data));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_oversized_upload_returns_false(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    u8 data[8] = {0};

    // size alone exceeds the buffer.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 0, 128));
    // offset + size exceeds the buffer, neither alone would.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 60, 8));
    // offset == buffer size, any positive size is out of bounds.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 64, 1));
    // offset alone is already out of bounds; offset + size must not wrap uint32_t.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, UINT32_MAX - 4, 8));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_buffer_destroy(nullptr); }

int main(void) {
    test_create_each_usage_succeeds();
    test_upload_in_bounds_succeeds();
    test_upload_at_offset_succeeds();
    test_oversized_upload_returns_false();
    test_destroy_null_is_noop();
    printf("test_gfx_buffer: OK\n");
    return 0;
}
```

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target test_gfx_buffer test_header_bk_gfx_buffer`
Run: `ctest --test-dir build --output-on-failure -R 'test_gfx_buffer|test_header_bk_gfx_buffer'`
Expected: clean build, zero warnings, all pass.

- [ ] **Step 6: Commit**

```bash
git add include/bielik/bk_gfx_buffer.h src/internal/bk_gfx_buffer_internal.h \
        src/bk_gfx_buffer.c tests/test_gfx_buffer.c
git commit -m "migrate bk_gfx_buffer to bk_types.h"
```

---

## Task 7: `bk_gfx_texture` migration

**Files:**
- Modify: `include/bielik/bk_gfx_texture.h`
- Modify: `src/bk_gfx_texture.c`
- Modify: `tests/test_gfx_texture.c`
- No changes: `src/internal/bk_gfx_texture_internal.h` (only declares SDL-type handle accessors, no primitives)

**Interfaces:**
- Consumes: `bk__alloc`/`bk__free` (Task 2).
- Produces: `BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage, i32 width, i32 height)` — Task 9's samples (`04_textured_quad`, `05_compute`) call this.

`struct BK_GfxTexture`'s private `width`/`height` fields (currently `Uint32`) become `u32`, not left as `Uint32`: unlike `bk_gfx.c`'s `swap_w`/`swap_h` (populated directly by an SDL out-parameter, a hard type constraint), this struct is entirely ours, and `bk_gfx_texture_create` already casts explicitly when storing into it — there's no technical reason to keep the SDL spelling here, and `u32` matches the sibling `BK_GfxBuffer.size` field from Task 6.

- [ ] **Step 1: Migrate `include/bielik/bk_gfx_texture.h`**

Replace the whole file:

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_types.h>

/// What a texture is used for.
typedef enum BK_GfxTextureUsage {
    BK_GFX_TEXTURE_USAGE_SAMPLER,        // CPU-uploaded, sampled by a fragment shader
    BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, // written by a compute shader, then sampled
} BK_GfxTextureUsage;

typedef enum BK_GfxFilter {
    BK_GFX_FILTER_NEAREST,
    BK_GFX_FILTER_LINEAR,
} BK_GfxFilter;

typedef enum BK_GfxAddressMode {
    BK_GFX_ADDRESS_CLAMP,
    BK_GFX_ADDRESS_REPEAT,
} BK_GfxAddressMode;

/// An R8G8B8A8_UNORM 2D texture -- the only format this module supports (a
/// sprite/atlas path). More formats get added when a real use case demands them.
typedef struct BK_GfxTexture BK_GfxTexture;

/// A sampler: filtering + addressing state, bound alongside a texture at draw time.
typedef struct BK_GfxSampler BK_GfxSampler;

/// Creates a width x height R8G8B8A8_UNORM texture for the given usage. Logs via
/// SDL_Log ("BK: " prefix) and returns nullptr on SDL_GPU failure. device is
/// explicit, matching bk_gfx_pipeline_create's precedent.
BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage, i32 width,
                                     i32 height);

/// Uploads width*height RGBA8 pixels (tightly packed, 4 bytes/pixel) into texture.
/// Only valid for a BK_GFX_TEXTURE_USAGE_SAMPLER texture -- BK_ASSERTs otherwise,
/// since uploading into a compute-write target is a programmer error, not a runtime
/// condition. Returns false and logs via SDL_Log on SDL_GPU failure.
bool bk_gfx_texture_upload(BK_GfxTexture *texture, const void *rgba_pixels);

/// Destroys a texture. No-op if texture is nullptr.
void bk_gfx_texture_destroy(BK_GfxTexture *texture);

/// Creates a sampler with the given filter and addressing mode (applied to both U and
/// V). Logs via SDL_Log and returns nullptr on SDL_GPU failure.
BK_GfxSampler *bk_gfx_sampler_create(SDL_GPUDevice *device, BK_GfxFilter filter,
                                     BK_GfxAddressMode address_mode);

/// Destroys a sampler. No-op if sampler is nullptr.
void bk_gfx_sampler_destroy(BK_GfxSampler *sampler);
```

- [ ] **Step 2: Migrate `src/bk_gfx_texture.c`**

Replace the whole file:

```c
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx_texture.h>

struct BK_GfxTexture {
    SDL_GPUDevice *device;
    SDL_GPUTexture *handle;
    BK_GfxTextureUsage usage;
    u32 width;
    u32 height;
};

struct BK_GfxSampler {
    SDL_GPUDevice *device;
    SDL_GPUSampler *handle;
};

BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage, i32 width,
                                     i32 height) {
    BK_ASSERT(device != nullptr);
    BK_ASSERT(width > 0);
    BK_ASSERT(height > 0);

    SDL_GPUTextureUsageFlags usage_flags = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (usage == BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET) {
        usage_flags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    }

    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = usage_flags,
        .width = (Uint32)width,
        .height = (Uint32)height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    SDL_GPUTexture *handle = SDL_CreateGPUTexture(device, &info);
    if (handle == nullptr) {
        SDL_Log("BK: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    BK_GfxTexture *texture = bk__alloc(sizeof(BK_GfxTexture));
    if (texture == nullptr) {
        SDL_ReleaseGPUTexture(device, handle);
        return nullptr;
    }
    texture->device = device;
    texture->handle = handle;
    texture->usage = usage;
    texture->width = (u32)width;
    texture->height = (u32)height;
    return texture;
}

bool bk_gfx_texture_upload(BK_GfxTexture *texture, const void *rgba_pixels) {
    BK_ASSERT(texture != nullptr);
    BK_ASSERT(rgba_pixels != nullptr);
    BK_ASSERT(texture->usage == BK_GFX_TEXTURE_USAGE_SAMPLER);

    u32 byte_size = texture->width * texture->height * 4;

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = byte_size,
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(texture->device, &transfer_info);
    if (transfer == nullptr) {
        SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(texture->device, transfer, false);
    if (mapped == nullptr) {
        SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
        return false;
    }
    SDL_memcpy(mapped, rgba_pixels, byte_size);
    SDL_UnmapGPUTransferBuffer(texture->device, transfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(texture->device);
    if (cmd == nullptr) {
        SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
        return false;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source = {.transfer_buffer = transfer,
                                         .pixels_per_row = texture->width,
                                         .rows_per_layer = texture->height};
    SDL_GPUTextureRegion destination = {
        .texture = texture->handle, .w = texture->width, .h = texture->height, .d = 1};
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(texture->device, transfer);
    return true;
}

void bk_gfx_texture_destroy(BK_GfxTexture *texture) {
    if (texture == nullptr) {
        return;
    }
    SDL_ReleaseGPUTexture(texture->device, texture->handle);
    bk__free(texture);
}

BK_GfxSampler *bk_gfx_sampler_create(SDL_GPUDevice *device, BK_GfxFilter filter,
                                     BK_GfxAddressMode address_mode) {
    BK_ASSERT(device != nullptr);

    SDL_GPUFilter sdl_filter =
        filter == BK_GFX_FILTER_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    SDL_GPUSamplerMipmapMode mipmap_mode = filter == BK_GFX_FILTER_LINEAR
                                               ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                               : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    SDL_GPUSamplerAddressMode sdl_address_mode = address_mode == BK_GFX_ADDRESS_REPEAT
                                                     ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                                                     : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    SDL_GPUSamplerCreateInfo info = {
        .min_filter = sdl_filter,
        .mag_filter = sdl_filter,
        .mipmap_mode = mipmap_mode,
        .address_mode_u = sdl_address_mode,
        .address_mode_v = sdl_address_mode,
        .address_mode_w = sdl_address_mode,
    };
    SDL_GPUSampler *handle = SDL_CreateGPUSampler(device, &info);
    if (handle == nullptr) {
        SDL_Log("BK: SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return nullptr;
    }

    BK_GfxSampler *sampler = bk__alloc(sizeof(BK_GfxSampler));
    if (sampler == nullptr) {
        SDL_ReleaseGPUSampler(device, handle);
        return nullptr;
    }
    sampler->device = device;
    sampler->handle = handle;
    return sampler;
}

void bk_gfx_sampler_destroy(BK_GfxSampler *sampler) {
    if (sampler == nullptr) {
        return;
    }
    SDL_ReleaseGPUSampler(sampler->device, sampler->handle);
    bk__free(sampler);
}

SDL_GPUTexture *bk__gfx_texture_handle(const BK_GfxTexture *texture) {
    BK_ASSERT(texture != nullptr);
    return texture->handle;
}

SDL_GPUSampler *bk__gfx_sampler_handle(const BK_GfxSampler *sampler) {
    BK_ASSERT(sampler != nullptr);
    return sampler->handle;
}
```

(`.width = (Uint32)width, .height = (Uint32)height,` inside `SDL_GPUTextureCreateInfo` stay `Uint32`-cast — that struct's fields are SDL's own, a hard boundary constraint, unlike `BK_GfxTexture`'s own fields above.)

- [ ] **Step 3: Migrate `tests/test_gfx_texture.c`**

Replace the whole file (`Vertex.position`/`.uv` → `f32[2]`, `.color` → `u8[4]`; `s_load_shader_file`'s `size_t *` → `usize *`; `s_check_pixel` gets the same treatment as Tasks 5's copies; `uint32_t pixels[4]` → `u32 pixels[4]`; `uint16_t indices[6]` → `u16 indices[6]`):

```c
#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include "internal/bk_gfx_buffer_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_GPUDevice *s_create_device(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);
    return device;
}

static void test_create_sampler_texture_and_upload_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxTexture *texture = bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
    REQUIRE(texture != nullptr);

    u32 pixels[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
    REQUIRE(bk_gfx_texture_upload(texture, pixels));

    bk_gfx_texture_destroy(texture);
    SDL_DestroyGPUDevice(device);
}

// Gate (spec §5/§9, plan Task 3): confirm SAMPLER | COMPUTE_STORAGE_WRITE actually
// creates on this machine's backend, and that R8G8B8A8_UNORM is a legal compute
// storage-write format -- both are load-bearing assumptions for samples/05_compute.
static void test_create_compute_target_texture_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxTexture *texture =
        bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET, 16, 16);
    REQUIRE(texture != nullptr);

    bk_gfx_texture_destroy(texture);
    SDL_DestroyGPUDevice(device);
}

static void test_create_samplers_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxSampler *nearest_clamp =
        bk_gfx_sampler_create(device, BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
    REQUIRE(nearest_clamp != nullptr);

    BK_GfxSampler *linear_repeat =
        bk_gfx_sampler_create(device, BK_GFX_FILTER_LINEAR, BK_GFX_ADDRESS_REPEAT);
    REQUIRE(linear_repeat != nullptr);

    bk_gfx_sampler_destroy(nearest_clamp);
    bk_gfx_sampler_destroy(linear_repeat);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) {
    bk_gfx_texture_destroy(nullptr);
    bk_gfx_sampler_destroy(nullptr);
}

// ---------------------------------------------------------------------------
// Golden-image test: a textured, indexed quad. Proves buffers, textures,
// samplers, and indexed drawing all work together -- the headless counterpart
// to samples/04_textured_quad. No window or swapchain needed, same pattern as
// tests/test_gfx_pipeline.c's golden-image test.
// ---------------------------------------------------------------------------

typedef struct Vertex {
    f32 position[2];
    f32 uv[2];
    u8 color[4];
} Vertex;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    REQUIRE(data != nullptr);
    return data;
}

static BK_GfxShaderDesc s_load_textured_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "textured.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "textured.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    // textured.frag declares one sampler2D; textured.vert declares none. This count
    // must match the shader binary's actual declaration (SDL_GPU validates it).
    if (SDL_strcmp(stage, "fragment") == 0) {
        desc.num_samplers = 1;
    }
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

static void s_check_pixel(const u8 *pixels, int width, int x, int y, u8 r, u8 g, u8 b, u8 a,
                          int tolerance) {
    usize i = ((usize)y * (usize)width + (usize)x) * 4;
    REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
    REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
    REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
    REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

// Checkerboard colors, hardcoded literals independent of the mutation test in
// test_draw_produces_expected_pixels_from_checkerboard below -- see the design spec
// §8 and the implementation plan Task 4 for why the assertions must not be re-derived
// from the uploaded array.
enum { RED_R = 255, RED_G = 0, RED_B = 0 };
enum { GREEN_R = 0, GREEN_G = 255, GREEN_B = 0 };
enum { BLUE_R = 0, BLUE_G = 0, BLUE_B = 255 };
enum { YELLOW_R = 255, YELLOW_G = 255, YELLOW_B = 0 };

static void test_draw_produces_expected_pixels_from_checkerboard(void) {
    constexpr int size = 64;
    constexpr int tolerance = 5;

    // Defensive, independent of test-function call order: see the identical call in
    // s_create_device above for why this is required.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex_shader = s_load_textured_shader("vertex");
    BK_GfxShaderDesc fragment_shader = s_load_textured_shader("fragment");

    BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
    BK_GfxVertexAttribute vertex_attributes[3] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, uv)},
        {.location = 2,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
         .offset = offsetof(Vertex, color)},
    };
    BK_GfxPipelineDesc pipeline_desc = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_buffers = &vertex_buffer_layout,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attributes,
        .num_vertex_attributes = 3,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
    REQUIRE(pipeline != nullptr);

    // A full-viewport quad: NDC (-1,-1) is the lower-left corner (SDL_GPU's
    // documented coordinate system, SDL_gpu.h "Coordinate System"), which maps uv
    // (0,1) -- the texture's bottom-left texel row -- so screen and texture agree on
    // which edge is "down". White vertex color leaves the sampled texel unmodified.
    Vertex vertices[4] = {
        {.position = {-1, -1}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
        {.position = {1, -1}, .uv = {1, 1}, .color = {255, 255, 255, 255}},
        {.position = {-1, 1}, .uv = {0, 0}, .color = {255, 255, 255, 255}},
        {.position = {1, 1}, .uv = {1, 0}, .color = {255, 255, 255, 255}},
    };
    BK_GfxBuffer *vertex_buffer =
        bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
    REQUIRE(vertex_buffer != nullptr);
    REQUIRE(bk_gfx_buffer_upload(vertex_buffer, vertices, 0, sizeof vertices));

    u16 indices[6] = {0, 1, 2, 2, 1, 3};
    BK_GfxBuffer *index_buffer =
        bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_INDEX, sizeof indices);
    REQUIRE(index_buffer != nullptr);
    REQUIRE(bk_gfx_buffer_upload(index_buffer, indices, 0, sizeof indices));

    // 2x2 checkerboard, row-major top-to-bottom matching SDL_UploadToGPUTexture's
    // pixels_per_row/rows_per_layer convention: texel row 0 is the texture's top row
    // (uv v=0), so this lays out red/green on top, blue/yellow on the bottom.
    u8 checkerboard[2][2][4] = {
        {{RED_R, RED_G, RED_B, 255}, {GREEN_R, GREEN_G, GREEN_B, 255}},
        {{BLUE_R, BLUE_G, BLUE_B, 255}, {YELLOW_R, YELLOW_G, YELLOW_B, 255}},
    };
    BK_GfxTexture *texture = bk_gfx_texture_create(device, BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
    REQUIRE(texture != nullptr);
    REQUIRE(bk_gfx_texture_upload(texture, checkerboard));

    BK_GfxSampler *sampler =
        bk_gfx_sampler_create(device, BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
    REQUIRE(sampler != nullptr);

    SDL_GPUTextureCreateInfo offscreen_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = size,
        .height = size,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    SDL_GPUTexture *offscreen = SDL_CreateGPUTexture(device, &offscreen_info);
    REQUIRE(offscreen != nullptr);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    REQUIRE(cmd != nullptr);

    SDL_GPUColorTargetInfo color_target = {
        .texture = offscreen,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pipeline));
    SDL_GPUBufferBinding vertex_binding = {.buffer = bk__gfx_buffer_handle(vertex_buffer)};
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_GPUBufferBinding index_binding = {.buffer = bk__gfx_buffer_handle(index_buffer)};
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_GPUTextureSamplerBinding sampler_binding = {.texture = bk__gfx_texture_handle(texture),
                                                    .sampler = bk__gfx_sampler_handle(sampler)};
    SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
    SDL_EndGPURenderPass(pass);

    void *pixels_buf = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(pixels_buf != nullptr);
    const u8 *pixels = (const u8 *)pixels_buf;

    // Quadrant centers. See the coordinate-system comment on `vertices` above for why
    // top-left of the downloaded image is texel (0,0), etc. -- this is fully
    // determined by SDL_GPU's documented convention, not backend-dependent.
    s_check_pixel(pixels, size, size / 4, size / 4, RED_R, RED_G, RED_B, 255, tolerance);
    s_check_pixel(pixels, size, 3 * size / 4, size / 4, GREEN_R, GREEN_G, GREEN_B, 255, tolerance);
    s_check_pixel(pixels, size, size / 4, 3 * size / 4, BLUE_R, BLUE_G, BLUE_B, 255, tolerance);
    s_check_pixel(pixels, size, 3 * size / 4, 3 * size / 4, YELLOW_R, YELLOW_G, YELLOW_B, 255,
                  tolerance);

    bk__free(pixels_buf);

    bk_gfx_pipeline_destroy(pipeline);
    bk_gfx_buffer_destroy(vertex_buffer);
    bk_gfx_buffer_destroy(index_buffer);
    bk_gfx_texture_destroy(texture);
    bk_gfx_sampler_destroy(sampler);
    s_free_shader(&vertex_shader);
    s_free_shader(&fragment_shader);
    SDL_ReleaseGPUTexture(device, offscreen);
    SDL_DestroyGPUDevice(device);
}

int main(void) {
    test_create_sampler_texture_and_upload_succeeds();
    test_create_compute_target_texture_succeeds();
    test_create_samplers_succeeds();
    test_destroy_null_is_noop();
    test_draw_produces_expected_pixels_from_checkerboard();
    printf("test_gfx_texture: OK\n");
    return 0;
}
```

(`<stddef.h>` stays — `offsetof` needs it.)

- [ ] **Step 4: Build and test**

Run: `cmake --build build --target test_gfx_texture test_header_bk_gfx_texture`
Run: `ctest --test-dir build --output-on-failure -R 'test_gfx_texture|test_header_bk_gfx_texture'`
Expected: clean build, zero warnings, all pass.

- [ ] **Step 5: Commit**

```bash
git add include/bielik/bk_gfx_texture.h src/bk_gfx_texture.c tests/test_gfx_texture.c
git commit -m "migrate bk_gfx_texture to bk_types.h"
```

---

## Task 8: `test_arena.c` migration

The one remaining test file with its own `size_t`/`uint8_t` usage not already covered by an earlier task (it exercises `bk_frame_alloc`, migrated in Task 2, but wasn't migrated alongside it since it's arena-specific, not app-lifecycle-specific).

**Files:**
- Modify: `tests/test_arena.c`

**Interfaces:**
- Consumes: `void *bk_frame_alloc(usize size, usize align)`, `void bk__arena_reset(void)` (Task 2).

- [ ] **Step 1: Migrate `tests/test_arena.c`**

Replace the whole file (`size_t` → `usize` throughout, including loop counters explicitly typed `size_t i`/`size_t b` — the exempt name `b` stays, only its type changes; `uint8_t` → `u8`; `uintptr_t` stays untouched, it's not one of the 13 types in `bk_types.h`, and `<stdint.h>` stays included for it):

```c
#include "bk_test.h"
#include "internal/bk_app_internal.h"
#include <bielik/bk_app.h>
#include <stdint.h>
#include <string.h>

static void test_alignment_across_powers_of_two(void) {
    const usize aligns[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    for (usize i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        bk__arena_reset();
        void *ptr = bk_frame_alloc(37, aligns[i]);
        REQUIRE(ptr != nullptr);
        REQUIRE(((uintptr_t)ptr % aligns[i]) == 0);
    }
}

static void test_reset_rewinds_cursor(void) {
    bk__arena_reset();
    void *p1 = bk_frame_alloc(64, 0);
    bk__arena_reset();
    void *p2 = bk_frame_alloc(64, 0);
    REQUIRE(p1 == p2);
}

static void test_growth_preserves_contents_and_stays_aligned(void) {
    bk__arena_reset();

    unsigned char *marker_before_growth = (unsigned char *)bk_frame_alloc(256, 0);
    REQUIRE(marker_before_growth != nullptr);
    memset(marker_before_growth, 0xAB, 256);

    const usize chunk_size = 64 * 1024;
    const int chunk_count = 90;
    for (int i = 0; i < chunk_count; i++) {
        void *ptr = bk_frame_alloc(chunk_size, 0);
        REQUIRE(ptr != nullptr);
        REQUIRE(((uintptr_t)ptr % alignof(max_align_t)) == 0);
    }

    bk__arena_reset();
    unsigned char *marker_after_growth = (unsigned char *)bk_frame_alloc(256, 0);
    REQUIRE(marker_after_growth != nullptr);
    for (usize i = 0; i < 256; i++) {
        REQUIRE(marker_after_growth[i] == 0xAB);
    }

    const usize aligns[] = {8, 16, 32, 64, 128, 256};
    for (usize i = 0; i < sizeof(aligns) / sizeof(aligns[0]); i++) {
        void *ptr = bk_frame_alloc(97, aligns[i]);
        REQUIRE(ptr != nullptr);
        REQUIRE(((uintptr_t)ptr % aligns[i]) == 0);
    }
}

static void test_growth_with_large_align(void) {
    bk__arena_reset();
    void *big = bk_frame_alloc(9u * 1024 * 1024, 1024 * 1024);
    REQUIRE(big != nullptr);
    REQUIRE(((uintptr_t)big % (1024 * 1024)) == 0);
}

static void test_interleaved_allocations_do_not_overlap(void) {
    bk__arena_reset();

    enum { CHUNK_COUNT = 24 };
    static const usize sizes[] = {1, 3, 7, 16, 33, 100, 5};
    static const usize aligns[] = {1, 4, 8, 16, 32};

    unsigned char *ptrs[CHUNK_COUNT];
    usize chunk_sizes[CHUNK_COUNT];
    u8 patterns[CHUNK_COUNT];

    for (int i = 0; i < CHUNK_COUNT; i++) {
        usize size = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        usize align = aligns[i % (sizeof(aligns) / sizeof(aligns[0]))];
        unsigned char *ptr = (unsigned char *)bk_frame_alloc(size, align);
        REQUIRE(ptr != nullptr);
        REQUIRE(((uintptr_t)ptr % align) == 0);

        u8 pattern = (u8)(i + 1);
        memset(ptr, pattern, size);

        ptrs[i] = ptr;
        chunk_sizes[i] = size;
        patterns[i] = pattern;
    }

    for (int i = 0; i < CHUNK_COUNT; i++) {
        for (usize b = 0; b < chunk_sizes[i]; b++) {
            REQUIRE(ptrs[i][b] == patterns[i]);
        }
    }
}

int main(void) {
    test_alignment_across_powers_of_two();
    test_reset_rewinds_cursor();
    test_growth_preserves_contents_and_stays_aligned();
    test_growth_with_large_align();
    test_interleaved_allocations_do_not_overlap();
    printf("test_arena: OK\n");
    return 0;
}
```

- [ ] **Step 2: Build and test**

Run: `cmake --build build --target test_arena`
Run: `ctest --test-dir build --output-on-failure -R test_arena`
Expected: clean build, zero warnings, all pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_arena.c
git commit -m "migrate test_arena.c to bk_types.h"
```

---

## Task 9: Samples migration (`01_clear` .. `05_compute`)

All five samples implement `BK_AppDesc`'s callbacks, so all five have the `const BK_FrameInfo *f` → `*frame` and `const SDL_Event *e` → `*event` rename, plus the `AppState *s = state;` → `AppState *app = state;` local (and every `s->` → `app->` reference) in every callback that does it. `AppState`'s own fields (`frame_count`, `frame_limit`, etc.) stay plain `int`/`bool` — they're sample-private bookkeeping, not part of bielik's public API, so the "plain int converts only at public-API-parameter/field boundaries" rule (established in Task 2) leaves them alone. Where a sample declares its own `float`/`double`/`uint8_t`/`uint16_t`/`size_t` locals or struct fields, those convert (that rule has no such carve-out).

**Files:**
- Modify: `samples/01_clear/main.c`
- Modify: `samples/02_ticks/main.c`
- Modify: `samples/03_triangle/main.c`
- Modify: `samples/04_textured_quad/main.c`
- Modify: `samples/05_compute/main.c`

**Interfaces:**
- Consumes: every public signature from Tasks 2, 4, 5, 6, 7 (`BK_AppDesc` callbacks, `bk_gfx_*`, `bk_gfx_pipeline_*`, `bk_gfx_buffer_*`, `bk_gfx_texture_*`). This task only compiles if Tasks 2–7 are already done.

- [ ] **Step 1: Migrate `samples/01_clear/main.c`**

Replace the whole file (`const BK_FrameInfo *f` → `*frame`, `const SDL_Event *e` → `*event`, `AppState *s` → `*app`, `float t` → `f32 elapsed`):

```c
// 01_clear — the smallest possible Bielik2D app.
//
// Demonstrates: the BK_APP entry-point macro, the init/update/render/event
// callback shape, reading command-line args in init, and driving gfx state
// (the clear color) from render. Built twice by CMake: once as `01_clear`
// (the default BK_APP + SDL main-callbacks path) and once as
// `01_clear_run` (the alternate bk_run()-direct path, for tools/tests that
// want to own their own main()) — see the #ifdef at the bottom.

#include <bielik/bk_gfx.h>
#include <bielik/bk_main.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// App state — a file static works fine for a single-app-at-a-time sample; a
// real game with more complex lifetime needs might allocate this instead.
// This sample only needs a frame counter and an optional frame limit.
typedef struct AppState {
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

// init: SDL isn't touched here directly — the framework has already created
// the window and GPU device by the time init runs. This is where you parse
// command-line args and set up your own game state.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }
    *state = &s_state;
    return BK_CONTINUE;
}

// update: runs once per frame in variable-dt mode (tick_hz == 0). Returning
// anything other than BK_CONTINUE ends the app — used here to support
// `--frames N` for CI smoke testing, so the sample terminates on its own
// instead of waiting for a window close.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    app->frame_count++;
    if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: the whole point of this sample — cycle the swapchain clear color
// through a slow rainbow using sinf(real_time), phase-shifted per channel.
// bk_gfx_set_clear_color just records the color; the framework's frame
// pipeline calls bk__gfx_flush (clear + present) right after render returns.
static void app_render(void *state, const BK_FrameInfo *frame) {
    (void)state;
    f32 elapsed = (f32)frame->real_time;
    BK_Color color = {
        .r = 0.5f + 0.5f * sinf(elapsed),
        .g = 0.5f + 0.5f * sinf(elapsed + 2.0943951f), // +2*pi/3
        .b = 0.5f + 0.5f * sinf(elapsed + 4.1887902f), // +4*pi/3
        .a = 1.0f,
    };
    bk_gfx_set_clear_color(color);
}

// event: the framework forwards every SDL event here since this app sets
// .event on its BK_AppDesc — with .event set, WE own quit handling
// entirely (the framework's built-in SDL_EVENT_QUIT-only handling only
// kicks in when .event is left NULL).
static BK_Result app_event(void *state, const SDL_Event *event) {
    (void)state;
    if (event->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// Two ways to boot this app, picked at compile time:
//
// Default (BK_APP): SDL owns main() via its main-callbacks machinery.
// This is the blessed path for a real game.
//
// BK_MAIN_HANDLED (defined by the `01_clear_run` CMake target below): we
// write our own main() and call bk_run() ourselves — the path meant for
// tools/tests that need to own their own entry point.
#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event, )
#endif
```

- [ ] **Step 2: Migrate `samples/02_ticks/main.c`**

Replace the whole file (same renames as Step 1, plus `double last_print_real_time` → `f64`):

```c
// 02_ticks — fixed 60Hz timestep, live tick/render stats, and a deliberate
// hitch demo.
//
// Unlike 01_clear (variable-dt: one update per frame, dt = whatever the
// frame took), this sample runs a FIXED 60Hz simulation step. The frame
// pipeline can run update() zero, one, or several times in a single frame
// depending on how far real time has drifted from sim time (see bk_time.h)
// — that's why renders/sec and ticks/sec are tracked separately below.
//
// Press SPACE to force a 300ms hitch (see update()) and watch the printed
// stats: the frame right after the hitch runs the maximum 8 fixed ticks in
// one go (max_ticks_per_frame — the spiral-of-death cap engaging), and
// whatever backlog is left beyond that cap gets discarded outright rather
// than chased down over several more frames (bk_clock's cap-hit branch
// resets the accumulator to its sub-tick remainder, it doesn't carry the
// rest forward). So the one-second stats window containing the hitch shows
// ticks/sec *dip* (fewer ticks landed in that window, not more) and
// `drift` take a one-time permanent step further negative — not a gradual
// multi-frame climb. Either way, ticks/sec is back to a steady ~60 the very
// next second: bounded and stable, never exploding.

#include <bielik/bk_gfx.h>
#include <bielik/bk_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// App state — a file static works fine for a single-app-at-a-time sample; a
// real game with more complex lifetime needs might allocate this instead.
//
// Tick/render counters are split because they're driven by different
// callbacks that don't run 1:1 in fixed-timestep mode: update() runs once
// per fixed 60Hz tick (0, 1, or several times per frame), render() runs
// exactly once per frame. Both counters are reset every time the once-a-
// second stats line is printed.
typedef struct AppState {
    int tick_count;              // ticks since boot; used for --frames N termination
    int frame_limit;             // 0 => no limit (the default; run until closed/ESC)
    int ticks_this_second;       // ticks since the last stats line
    int renders_this_second;     // frames rendered since the last stats line
    f64 last_print_real_time;    // frame->real_time as of the last stats line
    bool hitch_requested;        // set by event() on SPACE, consumed by update()
} AppState;

static AppState s_state;

// init: SDL isn't touched here directly — the framework has already created
// the window and GPU device by the time init runs. This is where you parse
// command-line args and set up your own game state.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state = (AppState){0};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }
    bk_gfx_set_clear_color((BK_Color){0.05f, 0.05f, 0.08f, 1.0f});
    *state = &s_state;
    return BK_CONTINUE;
}

// update: runs once per fixed 60Hz tick — possibly several times in a single
// frame if the app fell behind real time (up to the max_ticks_per_frame
// cap set below), possibly zero times in a frame that arrives faster than
// the tick rate. `--frames N` is interpreted as "run for N fixed ticks"
// here (not N rendered frames) since fixed ticks are the deterministic
// unit in this mode — checking the limit here is exactly where 01_clear's
// equivalent variable-dt check lives, just against the tick counter
// instead of the frame counter.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    app->tick_count++;
    app->ticks_this_second++;

    if (app->hitch_requested) {
        app->hitch_requested = false;
        // Deliberately naughty: never block inside update() in real game
        // code — a real game would stall input, physics, and audio right
        // along with this thread. Doing it here, once, on purpose, is the
        // whole point of this sample: it manufactures the kind of stall
        // bk_clock's hitch clamp (max_frame_dt) and spiral-of-death cap
        // (max_ticks_per_frame) exist to survive. The very next frame after
        // this delay runs the maximum 8 ticks at once (the cap engaging)
        // and permanently drops whatever backlog is left beyond that —
        // so the next printed stats line shows a *dip* in ticks/sec for
        // that one-second window (not a climb toward hundreds of ticks),
        // and settles back to ~60 right after.
        SDL_Delay(300);
    }

    if (app->frame_limit > 0 && app->tick_count >= app->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: runs once per frame (regardless of how many ticks ran this
// frame, including zero). This is where render-rate tracking and the
// once-a-second stats line live, since render always has exactly one call
// per frame and gets the interpolation alpha / real_time / sim_time fields
// the tick loop doesn't.
static void app_render(void *state, const BK_FrameInfo *frame) {
    AppState *app = state;
    app->renders_this_second++;

    if (frame->real_time - app->last_print_real_time >= 1.0) {
        // drift = how far the fixed-tick simulation clock has diverged
        // from the wall clock. It should stay small and bounded (well
        // under a second) even across the hitch demo above — that's the
        // clamp/cap combo doing its job instead of letting sim_time run
        // away trying to fully "catch up" in one shot.
        f64 drift = frame->sim_time - frame->real_time;
        printf("renders/sec=%d ticks/sec=%d alpha=%.3f drift=%.4fs\n", app->renders_this_second,
               app->ticks_this_second, frame->alpha, drift);
        app->renders_this_second = 0;
        app->ticks_this_second = 0;
        app->last_print_real_time = frame->real_time;
    }
}

// event: the framework forwards every SDL event here since this app sets
// .event on its BK_AppDesc — with .event set, WE own quit handling
// entirely (the framework's built-in SDL_EVENT_QUIT-only handling only
// kicks in when .event is left NULL). SPACE just sets a flag; the actual
// (deliberately bad) blocking happens in update(), never here.
static BK_Result app_event(void *state, const SDL_Event *event) {
    AppState *app = state;
    if (event->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            return BK_DONE;
        }
        if (event->key.key == SDLK_SPACE) {
            app->hitch_requested = true;
        }
    }
    return BK_CONTINUE;
}

// Two ways to boot this app, picked at compile time — see 01_clear's main.c
// for the full explanation. Note the explicit tick_hz/max_ticks_per_frame
// below: 8 is also the framework's own default (see bk_app.c), but this
// sample sets it explicitly since the spiral-of-death cap is the whole
// point of the demo, not an incidental default.
#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .time = {.tick_hz = 60, .max_ticks_per_frame = 8},
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.time = {.tick_hz = 60, .max_ticks_per_frame = 8}, .init = app_init, .update = app_update,
       .render = app_render, .event = app_event, )
#endif
```

- [ ] **Step 3: Migrate `samples/03_triangle/main.c`**

Replace the whole file (`size_t *out_size` → `usize *out_size`; same `frame`/`event`/`app` renames):

```c
// 03_triangle — the smallest possible use of bk_gfx_pipeline: load the precompiled
// shader bytecode produced offline (see shaders/triangle.{vert,frag} and
// cmake/shaders.cmake), build a pipeline, and draw a hardcoded triangle with no
// vertex buffer (the vertex shader generates positions from gl_VertexIndex). Built
// once, using the BK_APP entry-point macro like 01_clear.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_main.h>

#include <stdlib.h>
#include <string.h>

typedef struct AppState {
    BK_GfxPipeline *pipeline;
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    if (base_path == nullptr) {
        SDL_Log("BK: SDL_GetBasePath failed: %s", SDL_GetError());
        return nullptr;
    }

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    if (data == nullptr) {
        SDL_Log("BK: failed to load shader file %s: %s", path, SDL_GetError());
    }
    return data;
}

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

// init: create the pipeline here, once, up front -- the framework has already
// created the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);

    s_free_shader(&vertex);
    s_free_shader(&fragment);

    if (s_state.pipeline == nullptr) {
        return BK_FAIL;
    }

    *state = &s_state;
    return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as 01_clear/02_ticks.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    app->frame_count++;
    if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: bind the pipeline and draw 3 vertices. The framework's frame pipeline
// calls bk__gfx_flush (clear + bind/draw + present) right after render returns.
static void app_render(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    bk_gfx_bind_pipeline(app->pipeline);
    bk_gfx_draw(3);
}

static BK_Result app_event(void *state, const SDL_Event *event) {
    (void)state;
    if (event->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
    (void)result;
    AppState *app = state;
    bk_gfx_pipeline_destroy(app->pipeline);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
        .quit = app_quit,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
```

- [ ] **Step 4: Migrate `samples/04_textured_quad/main.c`**

Replace the whole file (`Vertex.position`/`.uv` → `f32[2]`, `.color` → `u8[4]`; `size_t *out_size` → `usize *out_size`; checkerboard buffer/indices → `u8`/`u16`; same `frame`/`event`/`app` renames; `CHECKERBOARD_SIZE`/`CHECKERBOARD_TILE` stay plain `int` — sample-local `constexpr`, not public API):

```c
// 04_textured_quad -- the smallest use of bk_gfx_buffer + bk_gfx_texture together: an
// uploaded vertex buffer (position/uv/color) and index buffer describe a quad, a
// procedurally generated checkerboard texture is sampled with NEAREST filtering. Built
// once, using the BK_APP entry-point macro like 01_clear/03_triangle.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_main.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex {
    f32 position[2];
    f32 uv[2];
    u8 color[4];
} Vertex;

constexpr int CHECKERBOARD_SIZE = 8;
constexpr int CHECKERBOARD_TILE = 1; // texels per checker square

typedef struct AppState {
    BK_GfxPipeline *pipeline;
    BK_GfxBuffer *vertex_buffer;
    BK_GfxBuffer *index_buffer;
    BK_GfxTexture *texture;
    BK_GfxSampler *sampler;
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    if (base_path == nullptr) {
        SDL_Log("BK: SDL_GetBasePath failed: %s", SDL_GetError());
        return nullptr;
    }

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    if (data == nullptr) {
        SDL_Log("BK: failed to load shader file %s: %s", path, SDL_GetError());
    }
    return data;
}

static BK_GfxShaderDesc s_load_textured_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "textured.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "textured.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    // textured.frag declares one sampler2D; textured.vert declares none.
    if (SDL_strcmp(stage, "fragment") == 0) {
        desc.num_samplers = 1;
    }
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

// init: build the pipeline, upload the quad's vertex/index data, and generate +
// upload a checkerboard texture, all up front -- the framework has already created
// the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    BK_GfxShaderDesc vertex_shader = s_load_textured_shader("vertex");
    BK_GfxShaderDesc fragment_shader = s_load_textured_shader("fragment");

    BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
    BK_GfxVertexAttribute vertex_attributes[3] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, uv)},
        {.location = 2,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
         .offset = offsetof(Vertex, color)},
    };
    BK_GfxPipelineDesc pipeline_desc = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_buffers = &vertex_buffer_layout,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attributes,
        .num_vertex_attributes = 3,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &pipeline_desc);

    s_free_shader(&vertex_shader);
    s_free_shader(&fragment_shader);

    if (s_state.pipeline == nullptr) {
        return BK_FAIL;
    }

    // A half-size quad centered on screen. NDC (-1,-1) is the lower-left corner
    // (SDL_GPU's documented coordinate system), which maps to uv (0,1) -- the
    // texture's bottom-left texel row -- so screen and texture agree on which edge is
    // "down".
    Vertex vertices[4] = {
        {.position = {-0.5f, -0.5f}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
        {.position = {0.5f, -0.5f}, .uv = {1, 1}, .color = {255, 255, 255, 255}},
        {.position = {-0.5f, 0.5f}, .uv = {0, 0}, .color = {255, 255, 255, 255}},
        {.position = {0.5f, 0.5f}, .uv = {1, 0}, .color = {255, 255, 255, 255}},
    };
    s_state.vertex_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
    if (s_state.vertex_buffer == nullptr ||
        !bk_gfx_buffer_upload(s_state.vertex_buffer, vertices, 0, sizeof vertices)) {
        return BK_FAIL;
    }

    u16 indices[6] = {0, 1, 2, 2, 1, 3};
    s_state.index_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_INDEX, sizeof indices);
    if (s_state.index_buffer == nullptr ||
        !bk_gfx_buffer_upload(s_state.index_buffer, indices, 0, sizeof indices)) {
        return BK_FAIL;
    }

    // Procedurally generate a checkerboard: alternating black/white squares.
    u8 checkerboard[CHECKERBOARD_SIZE][CHECKERBOARD_SIZE][4];
    for (int y = 0; y < CHECKERBOARD_SIZE; y++) {
        for (int x = 0; x < CHECKERBOARD_SIZE; x++) {
            bool light = ((x / CHECKERBOARD_TILE) + (y / CHECKERBOARD_TILE)) % 2 == 0;
            u8 value = light ? 255 : 32;
            checkerboard[y][x][0] = value;
            checkerboard[y][x][1] = value;
            checkerboard[y][x][2] = value;
            checkerboard[y][x][3] = 255;
        }
    }
    s_state.texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_SAMPLER,
                                            CHECKERBOARD_SIZE, CHECKERBOARD_SIZE);
    if (s_state.texture == nullptr || !bk_gfx_texture_upload(s_state.texture, checkerboard)) {
        return BK_FAIL;
    }

    s_state.sampler = bk_gfx_sampler_create(bk_gpu(), BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
    if (s_state.sampler == nullptr) {
        return BK_FAIL;
    }

    *state = &s_state;
    return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as 01_clear/02_ticks/03_triangle.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    app->frame_count++;
    if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: bind everything and draw 6 indices (two triangles forming the quad). The
// framework's frame pipeline calls bk__gfx_flush (clear + bind/draw + present) right
// after render returns.
static void app_render(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    bk_gfx_bind_pipeline(app->pipeline);
    bk_gfx_bind_vertex_buffer(app->vertex_buffer);
    bk_gfx_bind_index_buffer(app->index_buffer);
    bk_gfx_bind_texture(app->texture, app->sampler);
    bk_gfx_draw_indexed(6);
}

static BK_Result app_event(void *state, const SDL_Event *event) {
    (void)state;
    if (event->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
    (void)result;
    AppState *app = state;
    bk_gfx_sampler_destroy(app->sampler);
    bk_gfx_texture_destroy(app->texture);
    bk_gfx_buffer_destroy(app->index_buffer);
    bk_gfx_buffer_destroy(app->vertex_buffer);
    bk_gfx_pipeline_destroy(app->pipeline);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
        .quit = app_quit,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
```

- [ ] **Step 5: Migrate `samples/05_compute/main.c`**

Replace the whole file (same treatment as Step 4, plus `float params[8]` → `f32 params[8]`; `GRADIENT_SIZE` stays plain `int` — sample-local `constexpr`):

```c
// 05_compute -- the smallest use of bk_gfx_pipeline's compute support: a compute
// shader (gradient.comp) fills a texture once at init time, reading its parameters
// from an uploaded storage buffer, and the same textured-quad pipeline as
// 04_textured_quad draws it every frame. Built once, using the BK_APP entry-point
// macro like 01_clear/03_triangle/04_textured_quad.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_main.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex {
    f32 position[2];
    f32 uv[2];
    u8 color[4];
} Vertex;

constexpr int GRADIENT_SIZE = 256;

typedef struct AppState {
    BK_GfxPipeline *pipeline;
    BK_GfxBuffer *vertex_buffer;
    BK_GfxBuffer *index_buffer;
    BK_GfxTexture *texture;
    BK_GfxSampler *sampler;
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
    const char *base_path = SDL_GetBasePath();
    if (base_path == nullptr) {
        SDL_Log("BK: SDL_GetBasePath failed: %s", SDL_GetError());
        return nullptr;
    }

    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
    void *data = SDL_LoadFile(path, out_size);
    if (data == nullptr) {
        SDL_Log("BK: failed to load shader file %s: %s", path, SDL_GetError());
    }
    return data;
}

static BK_GfxShaderDesc s_load_textured_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "textured.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "textured.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    // textured.frag declares one sampler2D; textured.vert declares none.
    if (SDL_strcmp(stage, "fragment") == 0) {
        desc.num_samplers = 1;
    }
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

static BK_GfxComputePipelineDesc s_load_gradient_compute_desc(void) {
    BK_GfxComputePipelineDesc desc = {
        .num_readonly_storage_buffers = 1,
        .num_readwrite_storage_textures = 1,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
    };
    desc.spirv.code = s_load_shader_file("gradient.compute.spv", &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file("gradient.compute.msl", &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_compute_desc(BK_GfxComputePipelineDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

// Fills texture once via a compute dispatch: reads {base_color, scale} from a
// throwaway storage buffer and writes a gradient. bk_gfx_compute_dispatch is
// synchronous (see bk_gfx_pipeline.h), so this is deliberately called once here in
// init, not from render every frame.
static bool s_fill_gradient(BK_GfxTexture *texture) {
    BK_GfxComputePipelineDesc compute_desc = s_load_gradient_compute_desc();
    BK_GfxComputePipeline *compute_pipeline =
        bk_gfx_compute_pipeline_create(bk_gpu(), &compute_desc);
    s_free_compute_desc(&compute_desc);
    if (compute_pipeline == nullptr) {
        return false;
    }

    f32 params[8] = {0.1f, 0.2f, 0.6f, 1.0f, 0.8f, 0.6f, 0.0f, 0.0f};
    BK_GfxBuffer *params_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_READ, sizeof params);
    if (params_buffer == nullptr) {
        bk_gfx_compute_pipeline_destroy(compute_pipeline);
        return false;
    }
    if (!bk_gfx_buffer_upload(params_buffer, params, 0, sizeof params)) {
        bk_gfx_buffer_destroy(params_buffer);
        bk_gfx_compute_pipeline_destroy(compute_pipeline);
        return false;
    }

    BK_GfxBuffer *readonly_buffers[1] = {params_buffer};
    BK_GfxTexture *readwrite_textures[1] = {texture};
    BK_GfxComputeDispatchDesc dispatch_desc = {
        .pipeline = compute_pipeline,
        .readwrite_textures = readwrite_textures,
        .num_readwrite_textures = 1,
        .readonly_buffers = readonly_buffers,
        .num_readonly_buffers = 1,
        .groups_x = (GRADIENT_SIZE + 7) / 8,
        .groups_y = (GRADIENT_SIZE + 7) / 8,
        .groups_z = 1,
    };
    bool ok = bk_gfx_compute_dispatch(&dispatch_desc);

    bk_gfx_buffer_destroy(params_buffer);
    bk_gfx_compute_pipeline_destroy(compute_pipeline);
    return ok;
}

// init: build the graphics pipeline, upload the quad's vertex/index data, create the
// compute-target texture and fill it via a one-time dispatch -- the framework has
// already created the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    BK_GfxShaderDesc vertex_shader = s_load_textured_shader("vertex");
    BK_GfxShaderDesc fragment_shader = s_load_textured_shader("fragment");

    BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
    BK_GfxVertexAttribute vertex_attributes[3] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
         .offset = offsetof(Vertex, uv)},
        {.location = 2,
         .buffer_slot = 0,
         .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
         .offset = offsetof(Vertex, color)},
    };
    BK_GfxPipelineDesc pipeline_desc = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_buffers = &vertex_buffer_layout,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attributes,
        .num_vertex_attributes = 3,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &pipeline_desc);

    s_free_shader(&vertex_shader);
    s_free_shader(&fragment_shader);

    if (s_state.pipeline == nullptr) {
        return BK_FAIL;
    }

    // A full-viewport quad. NDC (-1,-1) is the lower-left corner (SDL_GPU's
    // documented coordinate system), which maps to uv (0,1) -- the texture's
    // bottom-left texel row -- so screen and texture agree on which edge is "down".
    Vertex vertices[4] = {
        {.position = {-1, -1}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
        {.position = {1, -1}, .uv = {1, 1}, .color = {255, 255, 255, 255}},
        {.position = {-1, 1}, .uv = {0, 0}, .color = {255, 255, 255, 255}},
        {.position = {1, 1}, .uv = {1, 0}, .color = {255, 255, 255, 255}},
    };
    s_state.vertex_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
    if (s_state.vertex_buffer == nullptr ||
        !bk_gfx_buffer_upload(s_state.vertex_buffer, vertices, 0, sizeof vertices)) {
        return BK_FAIL;
    }

    u16 indices[6] = {0, 1, 2, 2, 1, 3};
    s_state.index_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_INDEX, sizeof indices);
    if (s_state.index_buffer == nullptr ||
        !bk_gfx_buffer_upload(s_state.index_buffer, indices, 0, sizeof indices)) {
        return BK_FAIL;
    }

    s_state.texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET,
                                            GRADIENT_SIZE, GRADIENT_SIZE);
    if (s_state.texture == nullptr || !s_fill_gradient(s_state.texture)) {
        return BK_FAIL;
    }

    // LINEAR, unlike 04_textured_quad's NEAREST checkerboard -- the right choice for
    // a smooth gradient.
    s_state.sampler = bk_gfx_sampler_create(bk_gpu(), BK_GFX_FILTER_LINEAR, BK_GFX_ADDRESS_CLAMP);
    if (s_state.sampler == nullptr) {
        return BK_FAIL;
    }

    *state = &s_state;
    return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as the other samples.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    app->frame_count++;
    if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: bind everything and draw the same 6 indices every frame -- the gradient
// texture was filled once in init, not re-dispatched here.
static void app_render(void *state, const BK_FrameInfo *frame) {
    (void)frame;
    AppState *app = state;
    bk_gfx_bind_pipeline(app->pipeline);
    bk_gfx_bind_vertex_buffer(app->vertex_buffer);
    bk_gfx_bind_index_buffer(app->index_buffer);
    bk_gfx_bind_texture(app->texture, app->sampler);
    bk_gfx_draw_indexed(6);
}

static BK_Result app_event(void *state, const SDL_Event *event) {
    (void)state;
    if (event->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
    (void)result;
    AppState *app = state;
    bk_gfx_sampler_destroy(app->sampler);
    bk_gfx_texture_destroy(app->texture);
    bk_gfx_buffer_destroy(app->index_buffer);
    bk_gfx_buffer_destroy(app->vertex_buffer);
    bk_gfx_pipeline_destroy(app->pipeline);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
        .quit = app_quit,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
```

- [ ] **Step 6: Build every sample**

Run: `cmake --build build`
Expected: every target (library, all tests, all samples) now builds clean with zero warnings — this is the first point in the plan where the *entire* project compiles at once.

- [ ] **Step 7: Run the full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: every test passes (GPU-dependent tests need a real or software GPU device available locally — same requirement as today, unrelated to this migration).

- [ ] **Step 8: Smoke-test at least one sample manually**

Run: `./build/samples/01_clear/01_clear --frames 120` (or the equivalent built binary path on your platform)
Expected: exits 0, no crash — confirms the rainbow clear-color sample runs end to end with its migrated `f32`/`frame`/`app` identifiers.

- [ ] **Step 9: Commit**

```bash
git add samples/01_clear/main.c samples/02_ticks/main.c samples/03_triangle/main.c \
        samples/04_textured_quad/main.c samples/05_compute/main.c
git commit -m "$(cat <<'EOF'
migrate samples to bk_types.h and rename their single-letter identifiers

BK_FrameInfo *f -> *frame, SDL_Event *e -> *event, AppState *s -> *app
across all five samples; Vertex/checkerboard/gradient-param locals
pick up f32/u8/u16 where they were previously float/uint8_t/uint16_t.
EOF
)"
```

---

## Task 10: Completeness gate — grep sweep for stragglers

Every alias in `bk_types.h` is a transparent typedef, so a missed conversion compiles clean and passes every test. This task is the actual proof the migration is complete, not another build-and-test cycle — per spec §7.

**Files:** none created or modified unless the grep below turns up a straggler, in which case fix it the same way its sibling file was fixed in Tasks 1–9, then re-run the grep.

- [ ] **Step 1: Run the two completeness greps from the repo root**

```bash
grep -rnE '\b(uint(8|16|32|64)_t|int(8|16|32|64)_t|size_t|ptrdiff_t)\b' include/ src/ tests/ samples/
grep -rnE '\bfloat\b|\bdouble\b' include/ src/ tests/ samples/
```

- [ ] **Step 2: Confirm every surviving match is one of the pre-approved exemptions**

The only lines these two greps should still match, per spec §5/§7:
- `src/bk_gfx.c`: `Uint32 swap_w = 0, swap_h = 0;` and `bk__gfx_download_texture`'s `Uint32 width, Uint32 height` params (SDL_GPU call-boundary types).
- `src/internal/bk_gfx_internal.h`: the matching `bk__gfx_download_texture` declaration.
- `src/bk_gfx_texture.c`: the two `(Uint32)width`/`(Uint32)height` casts inside `SDL_GPUTextureCreateInfo` (SDL's own struct fields — not the surrounding `BK_GfxTexture.width`/`.height`, which are `u32` after Task 7).
- `tests/test_gfx_buffer.c`: `#include <stdint.h>` and the `UINT32_MAX` reference.
- `tests/test_arena.c`: `#include <stdint.h>` and every `uintptr_t` (not one of the 13 `bk_types.h` types, explicitly out of scope per spec §8).
- Any `Uint32`/`Uint8`/`Uint16`/`Sint32`/etc. spelled with SDL's own capitalization anywhere (SDL's typedefs, not `stdint.h`'s — the regex above only matches lowercase `uint*_t`/`int*_t`, so these won't even show up, but worth knowing they're correctly out of scope too).

If anything else shows up — a file the plan didn't touch, a type inside a struct/function this plan missed — that's an incomplete migration. Fix it in the same style as its module's task above (bare type → matching `bk_types.h` alias, rebuild, rerun that file's tests), then rerun both greps until only the list above remains.

- [ ] **Step 3: Full clean build and full test suite, once more**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
Run: `ctest --test-dir build --output-on-failure`
Expected: clean, all pass — this confirms nothing broke during any Step 2 fixups.

- [ ] **Step 4: Commit (only if Step 2 required fixes; otherwise skip — nothing to commit)**

```bash
git add -A
git commit -m "fix stragglers the fundamental-types grep gate caught"
```

---

## Task 11: `.clang-tidy` + CI step + `CLAUDE.md` doc line

Locks in the sweep so it doesn't silently regress. Lands advisory (`continue-on-error: true`), per spec §9 and §10 — new to this repo, untuned against SDL/Box2D-adjacent glue code.

**Files:**
- Create: `.clang-tidy`
- Modify: `.github/workflows/ci.yml`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Create `.clang-tidy`**

```yaml
Checks: '-*,readability-identifier-length'
CheckOptions:
  - key: readability-identifier-length.MinimumVariableNameLength
    value: '2'
  - key: readability-identifier-length.MinimumParameterNameLength
    value: '2'
  - key: readability-identifier-length.MinimumLoopCounterNameLength
    value: '2'
  - key: readability-identifier-length.IgnoredVariableNames
    value: '^(i|x|y|r|g|b|a)$'
  - key: readability-identifier-length.IgnoredParameterNames
    value: '^(i|x|y|r|g|b|a)$'
  - key: readability-identifier-length.IgnoredLoopCounterNames
    value: '^(i|x|y|r|g|b|a)$'
```

(Only `readability-identifier-length` — no `readability-identifier-naming` check, per spec §9/§10: it would immediately fail on `bk_types.h`'s own bare `i32`/`u32`/etc., which is a deliberate, already-approved design choice, not a bug to flag.)

- [ ] **Step 2: Verify locally before wiring into CI**

Run: `find include src -name '*.h' -o -name '*.c' | xargs clang-tidy -p build 2>&1 | tee /tmp/clang-tidy-report.txt`
(Requires a compilation database — if `build/compile_commands.json` doesn't exist, reconfigure with `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` first.)
Read the report. Expected: no findings inside `include/`/`src/` given Tasks 1–9 already eliminated every non-exempt single-letter identifier — if something unexpected surfaces (e.g. a name this plan didn't know about), that's real signal, not noise; fix it and rerun. Genuine noise (a finding inside vendored/generated code, or a check being overly pedantic about something not worth changing) gets suppressed with a targeted `// NOLINT` comment, not a blanket config change.

- [ ] **Step 3: Add the CI step**

In `.github/workflows/ci.yml`, add a new step after `Build` (before `Test`) on the `ubuntu-latest` leg only (clang-tidy needs the same clang toolchain as the build; the other two legs don't need a second copy of this check):

```yaml
      - name: clang-tidy (Ubuntu)
        if: matrix.os == 'ubuntu-latest'
        continue-on-error: true
        run: |
          cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
          find include src -name '*.h' -o -name '*.c' | xargs clang-tidy -p build
```

- [ ] **Step 4: Add the `CLAUDE.md` doc line**

In `CLAUDE.md`'s `## Conventions` → `Style:` list, add one line after the existing `.clang-format` bullet:

```markdown
- `.clang-tidy`: `readability-identifier-length` only (min 2 chars, `i`/`x`/`y`/`r`/`g`/`b`/`a`
  exempt) — enforces the no-single-letter-identifiers rule from this sweep. Advisory in CI
  (`continue-on-error: true`) until proven quiet; not yet wired into the default build.
```

- [ ] **Step 5: Commit**

```bash
git add .clang-tidy .github/workflows/ci.yml CLAUDE.md
git commit -m "$(cat <<'EOF'
add clang-tidy's readability-identifier-length check, advisory in CI

Locks in the single-letter-identifier sweep so new code can't quietly
regress it. Lands continue-on-error, same bootstrapping pattern as
the lavapipe GPU CI step, since this is new tooling untuned against
this codebase's SDL/Box2D-adjacent glue code.
EOF
)"
```

---

## Final verification checklist

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build` is clean (zero warnings, including `-Wshadow`).
- [ ] `ctest --test-dir build --output-on-failure` passes in full.
- [ ] Both grep patterns from Task 10 return only the pre-approved exemption list.
- [ ] `clang-tidy` (run manually per Task 11 Step 2) reports no findings in `include/`/`src/`.
- [ ] At least one sample runs manually (Task 9 Step 8) and one other sample is spot-checked visually if a display is available.
- [ ] `git log` shows one commit per task (11 commits, or 10 if Task 10 needed no fixes), each buildable and green on its own — not one giant squashed diff.
