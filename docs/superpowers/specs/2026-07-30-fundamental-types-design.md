# Bielik2D — Fundamental Types & Identifier Readability Design

## 0. Context and scope

Two related ergonomics changes, brainstormed together and shipped as one effort:

1. A new `bk_types.h` module defining short, fixed-width numeric type aliases
   (`i32`, `u32`, `f32`, ...) to replace `stdint.h`'s verbose names and bare
   `float`/`double`/`size_t` across the codebase.
2. A sweep renaming single-letter identifiers (function params and locals) that carry
   no domain meaning — e.g. `BK_Clock *c` — to descriptive names, everywhere the current
   Phase 0/1/2 code uses them.

Both are pure renames/aliases: no behavior changes, no new capabilities. This is not a
Phase 2 gfx-core sub-project in the roadmap sense (§7 of `PLAN.md`); it's a cross-cutting
convention change that touches every existing module. Per `PLAN.md` §4, the frozen
Phase 0/1 layout listing is not rewritten — this spec and its plan document the addition
the same way `bk_gfx_pipeline`, frame capture, and the buffers/textures/compute work did.

## 1. Module boundaries and file layout

One new header, no `.c` file — pure typedefs and `static_assert`s need no translation
unit of their own:

```
bielik2d/
  include/bielik/
    bk_types.h        (new)
    bk_app.h           (migrated)
    bk_gfx.h            "
    bk_gfx_buffer.h      "
    bk_gfx_pipeline.h    "
    bk_gfx_texture.h     "
    bk_main.h             "
    bk_task.h              "
    bk_time.h                "
  src/
    bk_app.c, bk_gfx.c, bk_gfx_buffer.c, bk_gfx_pipeline.c, bk_gfx_texture.c,
    bk_task.c, bk_time.c              (all migrated)
    internal/*.h                       (migrated where they carry numeric types)
  tests/
    test_header_bk_types.c   (new; standalone-compile stub)
    test_*.c                 (migrated)
  samples/
    01_clear .. 05_compute    (migrated)
```

## 2. `bk_types.h` — public API

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

// Bare, unprefixed names (Rust/Zig/Odin-style) traded deliberately against
// bielik2d's usual bk_/BK_ namespacing -- see §3 for the rationale and the
// collision risk this accepts.

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

`bk_types.h` must compile standalone (enforced the same way as every other public
header, via `tests/test_header_bk_types.c`).

## 3. Naming rationale and namespace risk

Three naming schemes were considered: bare (`f32`), `bk_`-prefixed (`bk_f32`), and
`BK_`-PascalCase (`BK_F32`). Bare wins on ergonomics — it's the whole point of this
module, and it's what every comparable modern systems language/library (Rust, Zig,
Odin, raylib, sokol) does for exactly this purpose.

The accepted tradeoff: bare global-namespace typedefs can collide with a future
dependency or with a game's own code that separately defines `f32`/`u32`/etc. None of
the currently locked dependencies (SDL3, Box2D v3, PhysFS, minicoro, cgltf, cimgui,
miniaudio/SDL3_mixer) define these names today. If a future dependency does collide,
the fix at that point is a local `#define`-guard or renaming the losing side — not a
reason to abandon bare names preemptively.

Full-width spellings (`int32`, `uint32`, `float64`, `bool32`) were considered and
rejected: they reintroduce most of the verbosity `stdint.h` already has, defeating the
purpose. Terseness here is treated as *part of* readability for this domain, not in
tension with it — consistent with how `i`/`x`/`y`/`r`/`g`/`b`/`a` are treated in §5.

## 4. Boundary with SDL / Box2D / other dependencies

No adapters, casts, or wrappers anywhere: `u32` *is* `uint32_t` *is* SDL's `Uint32` at
the representation level (all three are the same typedef chain), so passing a `u32`
into an SDL_GPU call, or receiving one back, is an implicit, zero-cost identity.
Existing call sites into SDL/Box2D APIs keep compiling unchanged; only the local
variable/parameter/field declarations around them change.

## 5. Migration: existing numeric types → `bk_types.h`

Applies everywhere: `include/bielik/*.h`, `src/*.c`, `src/internal/*.h`, `tests/*.c`,
`samples/*/main.c`.

**Converts:**
- `uint8_t`/`uint16_t`/`uint32_t`/`uint64_t` → `u8`/`u16`/`u32`/`u64`
- `int8_t`/`int16_t`/`int32_t`/`int64_t` → `i8`/`i16`/`i32`/`i64`
- `float` → `f32`, `double` → `f64`
- `size_t` → `usize`, `ptrdiff_t` → `isize` (none currently in the codebase, but the
  alias exists for when one shows up)
- plain data-carrying `int`/`unsigned` parameters, e.g. `bk_clock_init`'s `tick_hz` and
  `max_ticks_per_frame` → `i32`

**Does not convert:**
- `bool`/`true`/`false` (already C23 keywords; correct as-is)
- `BK_Result` and other `BK_`-enum types
- Loop-local counters and other locals with no data-carrying role beyond the loop
- Direct third-party API types at call sites (`SDL_GPUDevice *`, etc.) — unaffected
  per §4

Representative (non-exhaustive — the implementation plan enumerates every site):
`BK_Clock`'s `fixed_dt_ns`/`tick`/etc. (→ `u64`), `BK_ClockFrame.frame_dt`/`alpha`
(→ `f64`), `BK_GfxShaderDesc.code_size` (→ `usize`), `BK_GfxVertexAttribute.location`
(→ `u32`), `bk_frame_alloc(size_t size, size_t align)` (→ `usize`), `BK_TaskFn`'s
`start`/`end`/`worker_index` (→ `i32`/`u32`).

## 6. Identifier readability sweep

**Principle:** no single-letter names for functions, parameters, or locals, except
where the letter is itself the domain-standard token.

**Exempt (left as-is):**
- `i` — loop counters, and other locals playing the same structural role (e.g.
  `test_arena.c`'s byte-index loop variable `b` in `for (size_t b = 0; b <
  chunk_sizes[i]; b++)`)
- `x`, `y` — coordinates
- `r`, `g`, `b`, `a` — color channels (`BK_Color`, and the `uint8_t r, g, b, a`
  parameters in test pixel-comparison helpers), matching `SDL_FColor` and every other
  graphics API's convention field-for-field

**Renamed — known sites (implementation plan confirms full coverage):**
- `BK_Clock *c` → `BK_Clock *clock` — `bk_time.h`/`bk_time.c` (`bk_clock_init`,
  `bk_clock_advance`, `bk_clock_fixed_dt`, `bk_clock_sim_time`) and `tests/test_time.c`.
  `-Wshadow -Werror` is already enabled project-wide; `clock()` from `<time.h>` isn't
  transitively included by any current header, but the implementation step verifies
  this by building with `-DBK_WERROR=ON` rather than assuming it.
- `const BK_FrameInfo *f` → `const BK_FrameInfo *frame` — `bk_app.h`'s `update`/
  `fixed_update`/`render` callback signatures and every sample/test implementing them
  (`01_clear` .. `05_compute`, `test_app_lifecycle.c`, `test_gfx_capture.c`).
- `const SDL_Event *e` → `const SDL_Event *event` — `bk_app.h`'s `event` callback and
  its sample implementations.
- `BK_Result r` → `BK_Result result` — locals in `bk_app.c`'s frame loop.
- Other meaningless single-letter locals (e.g. `float t` in `samples/01_clear/main.c`,
  `size_t n` in `tests/test_time.c`) → renamed to what they hold (`elapsed`, `count`,
  etc.); exact names are an implementation-plan-level decision, not a spec-level one.

Scope matches §5: public headers, sources, internal headers, tests, samples.

## 7. Testing

- `tests/test_header_bk_types.c`: new stub, `#include <bielik/bk_types.h>` only —
  the standalone-compilation check, following the existing eight-file pattern.
  Registered in `tests/CMakeLists.txt` alongside the others.
- The `static_assert`s in `bk_types.h` itself are the correctness check for type
  sizes — they fire in every translation unit that includes the header, across every
  clang-everywhere target (clang, clang-cl, AppleClang, Emscripten). No separate
  runtime test needed for pure typedefs.
- No behavior changes anywhere in this effort, so the existing test suite
  (`test_time.c`, `test_task.c`, `test_arena.c`, `test_gfx*.c`, `test_app_lifecycle.c`)
  is the regression check: it must pass unmodified in substance (only signatures/types
  at call sites change) both before and after.
- `ctest --test-dir build --output-on-failure` with `-DBK_WERROR=ON` must be clean,
  including `-Wshadow`, after the identifier renames.

## 8. Explicitly out of scope

- No new types beyond the 13 listed in §2 (no `f16`/`float16`, no 128-bit ints, no
  vector/matrix types — those belong to a future math module, not this one).
- No change to `BK_Result`, enum naming, or the `bk_`/`BK_`/`bk__`/`s_` prefix
  conventions themselves — this only affects bare numeric types and single-letter
  identifiers.
- No renaming of struct field names that already carry domain meaning (`width`,
  `height`, `tick_hz`, etc.) — only single-letter and bare fixed-width-numeric cases
  are in scope.
- No wrapper/adapter layer at the SDL/Box2D boundary (see §4).

## 9. Enforcement — `clang-tidy`

`clang-format` only formats (whitespace, braces, wrapping) — it cannot enforce naming.
`clang-tidy` is the tool for that, and nothing in this repo runs it today (no
`.clang-tidy`, no CI step).

New `.clang-tidy` at the repo root enables two checks:

- `readability-identifier-length` — bans single-letter/short names for variables,
  parameters, and loop counters, with an `IgnoredVariableNames`/`IgnoredParameterNames`/
  `IgnoredLoopCounterNames` regex covering the exemptions from §6: `^(i|x|y|r|g|b|a)$`.
  This is what keeps the §6 sweep from silently regressing as new code is added.
- `readability-identifier-naming` — checks the `bk_`/`BK_`/`bk__`/`s_` conventions
  already documented in `CLAUDE.md`'s Conventions section but never mechanically
  enforced until now (functions: `bk_` + lower_case; types: `BK_` + CamelCase; enum
  constants: `BK_` + UPPER_CASE; internal linkage: `bk__`/`s_` prefixes).

**Landing plan** (bootstrapped the same way the lavapipe GPU CI step was): a new CI step
runs `clang-tidy` over the changed sources with `continue-on-error: true`. It reports
findings without failing the build. Once it's been quiet (or only flagging genuine
issues) across a few consecutive runs, the escape hatch comes out and it becomes a hard
gate, matching `-Werror`'s status today. Locally, it's runnable on demand — wiring it
into the default build (e.g. via `CMAKE_C_CLANG_TIDY`) is deferred until it's proven
quiet, to avoid slowing down every developer build with a noisy, still-being-tuned
checker.

`CLAUDE.md`'s Style section gets one line added noting `.clang-tidy` exists and what it
enforces, alongside the existing `.clang-format` line — this is implementation-time
documentation upkeep, not a new convention.

## 10. Decisions and rationale (do not relitigate in implementation sessions)

- Bare names over `bk_`/`BK_`-prefixed: ergonomics is the entire point; namespace
  collision risk accepted, see §3.
- Terse widths (`i32`, not `int32`) over full-width spellings: matches Rust/Zig/Odin/
  raylib/sokol precedent; treated as readable-by-convention within systems code, not in
  tension with the readability goal that motivated this effort.
- `usize`/`isize` included even though `size_t` is already short and idiomatic C: kept
  for consistency with the rest of the set and because `size_t`'s signed counterpart
  (`ptrdiff_t`) is not short or idiomatic.
- `b32` is additive only (GPU-layout structs), not a `bool` replacement — the doc
  comment on the typedef states this explicitly to prevent future misuse.
- `r`/`g`/`b`/`a` and `x`/`y` are treated as domain-standard tokens exempt from the
  single-letter-identifier rule, on the same footing as `i` — not spelled out.
- No PLAN.md §4 edit: the frozen Phase 0/1 layout listing stays as originally recorded;
  this addition is tracked here and in the accompanying plan, matching how every other
  Phase 2 addition has been documented so far.
- `clang-tidy` lands advisory (`continue-on-error: true`) rather than blocking: it's new
  to this repo and untuned against SDL/Box2D-adjacent glue code, so the lavapipe
  bootstrapping pattern (prove quiet, then remove the escape hatch) applies here too
  rather than risking an immediately-red, unrelated CI gate.
