# Deviations

Deviations from `PLAN.md`'s normative public API (section 6.1) or explicit
task instructions are recorded here, one entry per deviation, with a
one-paragraph rationale.

## bk_clock_advance cap-hit accumulator clamp (PLAN.md:408, task-P1-4-brief.md)

`PLAN.md` §6.4 (and the P1-4 task brief) specify the spiral-cap "drop the
backlog" clamp as `accumulator = min(accumulator, fixed_dt_ns)`. Worked
through with concrete numbers: whenever the cap is actually hit, the
uncapped tick count exceeds the capped one by at least 1, which means the
accumulator left over after subtracting `capped_ticks * fixed_dt_ns` is
*always* `>= fixed_dt_ns` — so `min(accumulator, fixed_dt_ns)` always
evaluates to exactly `fixed_dt_ns`, never less. That makes `alpha` exactly
`1.0` on every cap-hit frame, contradicting the same section's own
invariant ("alpha guaranteed `[0,1)`") and the task brief's explicit test 2
requirement that alpha stay `< 1.0` after a hitch. Implemented
`accumulator %= fixed_dt_ns` instead in the cap-hit branch, which keeps
only the sub-tick remainder, satisfies `[0,1)` for real, and matches the
"drop the backlog" intent (discard the full extra ticks' worth of time,
don't try to catch up).

## test_time.c spiral-cap-under-load parameters (PLAN.md:493-494, task-P1-4-brief.md)

Both the plan and the task brief specify the sustained-load spiral-cap test
as "100ms frames at 60Hz, `max_ticks_per_frame=8`, expect exactly 8 ticks
every call." At 60Hz the fixed step is ~16.667ms, so hitting an 8-tick cap
needs > 8*16.667ms ≈ 133.3ms of accumulated backlog per frame; 100ms only
ever produces 6 ticks/frame (100ms / 16.667ms), so the cap is never
actually hit at that frame size — the literal assertion would be false
regardless of implementation. `test_spiral_cap_sustained_load` in
`tests/test_time.c` keeps the brief's literal 100ms/10-frame case but
asserts the mathematically correct value (exactly 6 ticks/call, alpha always
`< 1.0`, no cap hit), and adds a second sub-case feeding 200ms frames (which
does exceed the cap threshold) asserting exactly 8 ticks/call — preserving
the test's evident intent (prove the cap holds under sustained overload)
without asserting a false statement about the 100ms case.

## bk_time.h include order

The task brief's header listing has `#include <stdint.h>` before
`#include <stdbool.h>`; `clang-format --dry-run --Werror` (mandated by the
same brief) sorts them alphabetically, so `stdbool.h` now comes first. No
type, field, or signature changed — API is otherwise byte-identical.

## Frame arena growth: recompute alignment from the post-grow base (PLAN.md §6.6, task-P1-6-brief.md)

The task brief's step-by-step algorithm computes `aligned_offset` once from
the pre-grow base, then (if growth is needed) reallocs and returns
`(new) base + aligned_offset` unchanged. That's only correct if the grown
block lands at an address congruent to the old base modulo `align` — true
for small aligns (malloc/realloc only guarantee `alignof(max_align_t)`,
typically 16 bytes) but not guaranteed for the larger aligns this task's own
test suite exercises (up to 4096). Implemented `bk_frame_alloc` to size the
grow target against a base-address-independent worst case
(`used + (align - 1) + size`) and compute the aligned offset once, after any
grow has already happened, from whatever base `bk__realloc` actually
returned — this keeps every returned pointer correctly aligned regardless of
where the backing buffer ends up, with no behavior change when growth
doesn't move the block (the overwhelming common case) or when `align` is
small enough that the distinction is moot.

## test_arena.c growth-content-preservation check avoids dereferencing a stale pointer (task-P1-6-brief.md)

The brief's growth test (requirement 3) describes holding a pointer from
before a growth-triggering allocation and dereferencing it afterward to
prove `bk__realloc`-based growth preserved contents — this is exactly the
"raw pointers become invalid if the block moves" caveat the same brief
calls out two paragraphs earlier as an inherent, out-of-scope limitation.
Tried the literal version first: on this machine (macOS, ASan-instrumented
allocator), the 4 MiB → 8 MiB grow *does* relocate the block, and
dereferencing the stale pre-grow pointer is a confirmed
AddressSanitizer heap-use-after-free, not a hypothetical one. Fixed by
exploiting the C standard's own malloc/realloc alignment guarantee instead:
write the marker at offset 0 of a freshly-reset arena (guaranteed, since
malloc/realloc always return `max_align_t`-suitably-aligned memory, so an
`align=0` request at `used == 0` always lands at `base + 0`), trigger
growth, then reset and re-request `bk_frame_alloc(256, 0)` again — this
returns a **fresh, valid** pointer to the arena's current base (same
logical offset-0 slot), and reading through it proves the bytes written
before growth survived the `realloc`, without ever reading through
potentially-freed memory.

## bk_gfx.h formatting (task-P1-7-brief.md)

The task brief's normative header listing writes `BK_Color` as a one-line
struct: `typedef struct BK_Color { float r, g, b, a; } BK_Color;`.
`clang-format --dry-run --Werror` (mandated by the same brief's verification
step) rejects that layout and forces the attached-brace multiline form
(`typedef struct BK_Color {\n    float r, g, b, a;\n} BK_Color;`) instead. No
type, field, or signature changed — same precedent as the P1-5 entry below
(clang-format collapsing brief-specified spacing) and the P1-6 report's
formatting fix.

## bk_app.h / bk_task.c formatting (task-P1-5-brief.md)

Two clang-format-forced deviations from the P1-5 brief's literal listings,
neither changing any type, field, or signature: (1) `BK_TaskSystemDesc.finish`
is written `void (*finish)(void *task, void *ctx);` (single space) instead of
the brief's `void  (*finish)(...)` (double space, presumably hand-aligned
with the `void *(*enqueue)` line above it) — clang-format collapses the extra
space. (2) `src/bk_task.c`'s `#include` block ends up
`"internal/bk_task_internal.h"` before `<bielik/bk_task.h>` before
`<stddef.h>` — clang-format's default include sorting orders the quoted
non-matching-stem include ahead of the angle-bracket group, the same
behavior already visible in `tests/test_time.c` (`"bk_test.h"` before
`<bielik/bk_time.h>`), so this follows established project precedent rather
than the brief's "own header first" prose convention.

## bk_run's return value is a process exit code, not a raw SDL_AppResult/BK_Result (PLAN.md §6.3, task-P1-8-brief.md)

`bk_run` is specified (both in `PLAN.md` §6.3 — "store desc in a static, then
`SDL_EnterAppMainCallbacks`... Return its result" — and in the P1-8 brief's
literal `bk_run` body) to return `SDL_EnterAppMainCallbacks`'s value
unmodified. The brief's own `test_app_lifecycle.c` listing then asserts
`REQUIRE(result == BK_DONE)` (i.e. expects `1`). Checked the real SDL3
implementation (`build/_deps/sdl3-src/src/main/generic/SDL_sysmain_callbacks.c`,
and confirmed by `SDL_main.h`'s own doc comment "\returns standard Unix main
return value"): `SDL_EnterAppMainCallbacks` ends with
`return (rc == SDL_APP_FAILURE) ? 1 : 0;` — a Unix exit code, not the
`SDL_AppResult` value. A clean `BK_DONE` termination therefore makes
`bk_run` return `0`, not `1`. Per `bk_run`'s own explicit body (which this
task's brief also specifies verbatim and which correctly matches "return its
result" from the plan), translating the value would be the actual deviation;
fixed `tests/test_app_lifecycle.c` to assert `REQUIRE(result == 0)` instead
of the brief's literal `REQUIRE(result == BK_DONE)`, preserving the test's
evident intent (prove `bk_run` reports the terminating callback's outcome
faithfully) without asserting a false statement about what
`SDL_EnterAppMainCallbacks` actually returns.

## bk_main.h needs a forward declaration of bk__app_desc (task-P1-8-brief.md Part 6)

The brief's (and `PLAN.md`'s) `bk_main.h` listing defines `SDL_AppInit`'s
body as `return bk__boot(bk__app_desc, appstate, argc, argv);` with no prior
declaration of `bk__app_desc` in the header — its only definition comes from
the `BK_APP(...)` macro invocation the *user's* translation unit writes,
textually *after* the `#include <bielik/bk_main.h>` line. Verified this is a
real, not hypothetical, compile failure: built a throwaway
`BK_APP(...)`-based scratch program per the brief's own suggestion (task
Part 7's closing note) and got
`error: use of undeclared identifier 'bk__app_desc'` under this project's
C23/clang-everywhere toolchain, which (unlike C11/C17) treats implicit
function declarations as a hard error rather than a warning. Added
`BK_AppDesc bk__app_desc(void);` as a forward declaration in `bk_main.h`,
right after the `BK_APP` macro definition and before `SDL_AppInit` — this
doesn't change the decision log's "the library never references
`bk__app_desc` directly" invariant (the *library*, i.e. `bk_app.c`/`libbielik.a`,
still never references it; only `bk_main.h`, included into the *user's* TU,
does), so `bk_run` users still incur no link-time dependency on the symbol.

## bk_app.c needs two includes beyond Part 5's explicit list (task-P1-8-brief.md)

Part 5's "Includes" section names only `<bielik/bk_time.h>` and
`"internal/bk_gfx_internal.h"` as additions, but two more turned out to be
required for the file to compile/link at all, given the rest of Part 5's own
step-by-step instructions: (1) `"internal/bk_task_internal.h"`, for
`bk__task_set_desc`, which step 9 of `bk__boot` explicitly calls; (2)
`<SDL3/SDL_main.h>`, for `SDL_EnterAppMainCallbacks`, which `SDL3/SDL.h`
deliberately does not pull in (its own doc comment: "SDL_main.h is special
and not included here"). The second one has a sharp edge worth recording on
its own: naively including `<SDL3/SDL_main.h>` without first defining
`SDL_MAIN_HANDLED` would, on platforms where `SDL_MAIN_AVAILABLE` gets
defined unconditionally (confirmed by reading the header: Windows, GDK, iOS/
tvOS, Android, Emscripten, PSP, PS2, 3DS — notably *not* macOS/Linux, which
is why this didn't surface as a build failure on this sandbox), both
`#define main SDL_main` and an `#include <SDL3/SDL_main_impl.h>` that
generates a real platform entry point — injecting a second, conflicting
`main`/`WinMain` implementation into the static library's own translation
unit, which would collide at link time with the one every `bk_main.h`-using
app already generates in its own TU. Added
`#define SDL_MAIN_HANDLED 1` before `bk_app.c`'s include block to get only
the `SDL_EnterAppMainCallbacks` declaration and the `SDL_AppInit_func`-family
typedefs, none of the platform-entry-point side effects.

## bk_app.h / bk_app.c hand-aligned struct-field spacing (task-P1-8-brief.md)

Same category as the two P1-5/P1-7 entries above: `clang-format --dry-run
--Werror` collapses the brief's hand-aligned double/triple-space field
columns (e.g. `BK_Result   sim_time;`, `BK_AppDesc     desc;`) to single
spaces throughout `BK_FrameInfo`, `BK_WindowDesc`, `BK_TimeDesc`,
`BK_AppDesc`, and the new file-static `BK_AppState` in `bk_app.c`. No type,
field, or signature changed anywhere; ran `clang-format -i` and kept its
output rather than fighting the tool, consistent with established
precedent.
