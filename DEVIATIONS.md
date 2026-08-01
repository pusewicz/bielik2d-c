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

## bk_clock_init negative BK_TimeDesc field clamps (final-review-fix-brief.md §2)

A whole-branch review found that `bk_clock_init` passed negative
`max_frame_dt`/`max_ticks_per_frame` straight through unvalidated: negative
`max_frame_dt` hits UB casting to `uint64_t` and in practice yields
`max_frame_ns == 0`; negative `max_ticks_per_frame` wraps to ~1.8e19 casting
to `uint64_t`, silently disabling the spiral-of-death cap. The fix brief's
own illustrative clamp code was `if (max_frame_dt < 0.0) { max_frame_dt =
0.0; }`. Implemented that literally first and ran it against the brief's own
required regression test ("confirm `bk_clock_advance` still produces sane,
non-zero `frame_dt` for a normal input delta"): it fails
(`tests/test_time.c:156: REQUIRE failed: f.frame_dt > 0.0`), because
`max_frame_ns == 0` clamps *every* frame's dt to zero forever regardless of
the sign of the original input — `raw_ns < 0` is never true for an unsigned
`raw_ns`, so the `else` branch (`frame_dt_ns = max_frame_ns = 0`) always
wins. Clamping to `0.0` removes the UB but reproduces the exact "clock never
advances" symptom the brief itself describes as the bug. Implemented `if
(max_frame_dt <= 0.0) { max_frame_dt = 0.25; }` instead — falling back to
the same 0.25s default `BK_TimeDesc.max_frame_dt`'s doc comment documents
and `bk__boot` already substitutes for the exactly-zero case — which passes
the brief's required test and makes `bk_clock_init` self-sufficient for any
direct caller, not just the `bk__boot` path. Also folds the `== 0.0` case
into the same clamp (previously only negative was targeted): nothing today
relies on `0.0` reaching `bk_clock_init` directly (every test and `bk__boot`
itself substitute `0.25` first), and `0.0` hits the identical "frozen clock"
bug for a hypothetical direct caller, so there's no reason to leave that
edge unhandled while fixing the negative one. `max_ticks_per_frame < 1 ->
1` is unchanged from the brief's own snippet — that one holds up under the
test (`f2.ticks == 1`, not thousands).

## bk_quit()'s doc comment overstated equivalence to BK_DONE (PLAN.md:228, final-review-fix-brief.md §14)

`include/bielik/bk_app.h`'s `bk_quit()` doc comment said "equivalent to
returning `BK_DONE`" unconditionally — true only when the app has no custom
`.event` handler (the framework's *built-in* `SDL_EVENT_QUIT` handling
returns `BK_DONE`). When `.event` is set, `bk__event` forwards everything
and the app owns quit handling entirely (per `PLAN.md` §6.3's quit rules,
correctly implemented), so a game could freely ignore the pushed
`SDL_EVENT_QUIT` — not equivalent in that case. `PLAN.md` §6.1 makes the
identical over-broad claim in its own doc comment for this function
("Convenience: pushes `SDL_EVENT_QUIT` (equivalent to returning
`BK_DONE`)."), so this is a genuine plan defect predating any task, not
something a task introduced. Fixed the doc comment in `bk_app.h` to state
the conditional accurately; `PLAN.md` itself is left as-is (per this task's
scope — only `CLAUDE.md`'s stale Project section and `PLAN.md` §4's
directory tree were in scope for edits) with this entry serving as the
record of the same defect there.

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

## samples/01_clear/main.c needs an explicit bk_gfx.h include (task-P1-10-brief.md)

The task brief's normative `main.c` listing includes only `<bielik/bk_main.h>`
plus a handful of libc headers, then uses `BK_Color` and
`bk_gfx_set_clear_color` in `app_render` without including
`<bielik/bk_gfx.h>` anywhere. Neither `bk_main.h` nor the `bk_app.h` it pulls
in re-exports `bk_gfx.h` (confirmed: `grep -rn "bk_gfx.h" include/ src/`
shows it's only included by `bk_gfx.c` itself and the gfx-specific test
files) — this is a real, not hypothetical, compile failure
(`error: use of undeclared identifier 'BK_Color'` /
`'bk_gfx_set_clear_color'`) building `01_clear` as specified. Added
`#include <bielik/bk_gfx.h>` above `<bielik/bk_main.h>` in `main.c`;
everything else in the listing is unchanged. Chose to fix the sample rather
than have `bk_app.h`/`bk_main.h` transitively include `bk_gfx.h`, since gfx
is its own module or a user could reasonably use `bk_run` without ever
touching gfx state.

## BK_APP(...) macro invocation collapses to one line (task-P1-10-brief.md)

Same category as the four prior clang-format-collapse entries above: the
brief's `main.c` listing hand-formats the `BK_APP(...)` call at the bottom
of the file across six lines (one designated initializer per line, trailing
comma, closing paren on its own line). The full call fits within the
100-column limit on one line, so `clang-format --dry-run --Werror` (the same
brief's own verification step) rejects the multi-line form and collapses it
to `BK_APP(.init = app_init, .update = app_update, .render = app_render,
.event = app_event, )`. No behavior change — same established precedent of
letting clang-format win rather than fighting the tool or introducing a
`// clang-format off/on` convention with no prior precedent in this repo.

## samples/02_ticks/main.c: BK_APP(...) macro invocation collapses (task-P1-11-brief.md)

Same category as the 01_clear entry directly above: the brief's `main.c`
listing hand-formats the closing `BK_APP(...)` call across eight lines (one
designated initializer per line, including the added `.time = {...}` field,
trailing comma, closing paren on its own line). `clang-format --dry-run
--Werror` (this brief's own verification step) rejects that layout and
reflows it to two lines within the 100-column limit:
`BK_APP(.time = {.tick_hz = 60, .max_ticks_per_frame = 8}, .init = app_init,
.update = app_update,\n       .render = app_render, .event = app_event, )`.
No behavior change — ran `clang-format -i` and kept its output rather than
fighting the tool, consistent with established precedent.

## samples/02_ticks/main.c: hitch-demo comments describe the wrong catch-up shape (task-P1-11-brief.md)

The brief's normative listing (both the file header comment and the
in-function comment inside `app_update`'s hitch branch) describes the
post-hitch behavior as "ticks/sec briefly climbs toward the 8-tick-per-frame
cap to catch up, then settles back to ~60" — implying a multi-frame ramp-up
in the *reported* ticks/sec metric. Checked against the actual
`bk_clock_advance` implementation (`src/bk_time.c`): on a cap hit, the
accumulator is reset via `accumulator_ns %= fixed_dt_ns` (see the first
`DEVIATIONS.md` entry above), which *discards* the backlog beyond
`max_ticks_per_frame` in that same single `bk_clock_advance` call rather than
carrying it forward for later frames to chase down — there is no multi-frame
"climb." Confirmed empirically during the manual hitch check: pressing SPACE
twice produced two isolated dips in the printed `ticks/sec` (one line showing
~50 instead of ~60, immediately followed by lines back at 60/61) and two
matching ~0.17s permanent steps in `drift` (-0.04s → -0.21s → -0.37s,
~167ms per hitch, consistent with 300ms hitch minus 8 ticks × 16.667ms
already run that frame) — never a value trending upward across several
consecutive lines. Rewrote both comments to describe the single-frame
cap-then-discard behavior and the resulting one-second dip instead of the
brief's climb-then-settle framing; no code or `.time = {...}` values changed.

## Shader toolchain substitutes glslc+spirv-cross/GLSL for shadercross+HLSL (docs/superpowers/specs/2026-07-29-phase2-shader-pipeline-design.md §2)

`CLAUDE.md`'s locked shader decision names SDL_shadercross (HLSL/SPIR-V ->
SPIR-V/DXIL/MSL) as the offline shader toolchain. Neither `shadercross` nor
DirectXShaderCompiler (DXC) is installed anywhere this sub-project was
implemented, and DXC has no prebuilt macOS binary anywhere in
SDL_shadercross's own tooling -- the only supported macOS path builds a
vendored LLVM/Clang fork from source, which is impractical to do as part of
implementing one shader pair. `glslc` (from Google's `shaderc`) and
`spirv-cross` are real, Homebrew-installable tools with no DXC dependency,
verified end to end (GLSL -> SPIR-V via `glslc`, SPIR-V -> MSL via
`spirv-cross`) before committing to this plan. `shaders/triangle.vert` and
`shaders/triangle.frag` are therefore authored in GLSL, not HLSL, and their
committed bytecode covers SPIR-V and MSL only -- no DXIL variant exists,
since no available tool can produce it without DXC.
`bk_gfx_pipeline_create` fails gracefully (logs, returns `nullptr`) on any
device that only supports DXIL (Windows/D3D12) until someone with real DXC
tooling generates the DXIL variant; CI's GPU-dependent tests are already
`continue-on-error: true` on every platform, so this doesn't block required
CI. Migrating to the shadercross/HLSL toolchain is future work once that
tooling is genuinely available, not a reversal of the locked decision.

## test_gfx_pipeline.c missing SDL_Init before SDL_CreateGPUDevice (task-2-brief.md)

The task-2 brief's verbatim `test_create_and_destroy_pipeline_succeeds` calls
`SDL_CreateGPUDevice` with no prior `SDL_Init`. Run as written, this fails:
`SDL_CreateGPUDevice` returns `nullptr` with error "Video subsystem not
initialized". Confirmed in SDL's own source
(`SDL_GPUSelectBackend`, `src/gpu/SDL_gpu.c`): it calls `SDL_GetVideoDevice()`
and errors out immediately if the video subsystem hasn't been initialized --
this is a hard precondition of `SDL_CreateGPUDevice`, not environment
flakiness. `test_app_lifecycle` (Phase 1) never hits this because it drives
`bk_run()`, which already calls `SDL_Init(SDL_INIT_VIDEO)` before creating its
GPU device; this test creates a device directly, with no app/window, so
nothing else in the call path initializes the video subsystem for it. Added a
single `SDL_Init(SDL_INIT_VIDEO);` as the first line of the test function,
before the `SDL_CreateGPUDevice` call. No corresponding `SDL_Quit` was added
-- the test has no other teardown and the process exits immediately after --
so this keeps the deviation to the minimum needed to make the verbatim
scenario (create a pipeline against a real device, no window) actually
runnable. `bk_gfx_pipeline_create` itself is unchanged from the brief: it
still takes `device` as an explicit, caller-supplied parameter and does not
call `SDL_Init` itself, consistent with the module's stated design (pipelines
creatable/testable without a running app or window). This does mean the
design spec's framing of the golden-image test as "verifiable in CI without a
display server at all" (§6) is slightly too strong on platforms where video
subsystem init itself requires a display server (Linux/X11/Wayland, hence
`xvfb-run` in CI; less of an issue on macOS/Windows) — the test still needs
no *window* or *swapchain*, but it does need `SDL_INIT_VIDEO`.

## src/bk_gfx_pipeline.c include order (task-2-brief.md)

Same category as the `bk_time.h` include-order entry above. The brief's
verbatim `src/bk_gfx_pipeline.c` lists
`#include "internal/bk_gfx_pipeline_internal.h"` before
`#include "internal/bk_app_internal.h"`; `clang-format --dry-run --Werror`
(mandated by the same brief) sorts same-block quoted includes alphabetically,
putting `bk_app_internal.h` first, and also reflows a few multi-line function
signatures' continuation-line indentation. Ran `clang-format -i` on the file;
no type, field, or function body changed.

## PLAN.md §6.1's type spellings and parameter names are superseded by the fundamental-types migration (PLAN.md:164-241)

`PLAN.md` §6.1 is `CLAUDE.md`'s named "normative spec for the public API," but its
code listings still show the pre-migration API: `BK_WindowDesc { int w, h; }`,
`BK_Clock *c`/`const BK_FrameInfo *f`/`const SDL_Event *e` parameter names,
`uint64_t tick`/`double sim_time` field types in `BK_FrameInfo`, `int32_t`/`uint32_t`
in the `BK_TaskFn` typedef, and `size_t size, size_t align` in `bk_frame_alloc`. The
fundamental-types migration (`docs/superpowers/specs/2026-07-30-fundamental-types-design.md`,
`docs/superpowers/plans/2026-07-31-fundamental-types-plan.md`) superseded every one of
these: `include/bielik/bk_types.h`'s short aliases replaced the `stdint.h`/`float`/
`double`/`size_t` spellings project-wide (`u64 tick`, `f64 sim_time`, `i32`/`u32` in
`BK_TaskFn`, `usize size, usize align` in `bk_frame_alloc`), and the identifier
readability sweep in the same effort renamed the single-letter parameters/fields
`PLAN.md` §6.1 still shows (`BK_Clock *clock`, `const BK_FrameInfo *frame`, `const
SDL_Event *event`, `BK_WindowDesc.width`/`.height`). Per this file's own convention,
`PLAN.md` §6.1's text is left as the frozen historical record of Phase 0/1's original
design rather than rewritten to match — this entry, plus the two documents above, are
the source of truth for the current header shape. Verified against
`include/bielik/bk_app.h`, `bk_time.h`, `bk_task.h`, and `bk_gfx.h` as they stand
today, not re-derived from the stale listing.

## bk_app.h trailing-comment column (fundamental-types-plan task-2-brief.md)

Same category as the `bk_app.h`/`bk_app.c` hand-aligned-spacing entry above,
recurring because the fundamental-types migration shortens several field
types (`uint64_t tick` -> `u64 tick`, `double sim_time` -> `f64 sim_time`),
which shifts where `clang-format`'s trailing-comment column lands.
`clang-format --dry-run --Werror` flagged one line in `BK_FrameInfo`
(`u64 tick;` needed two more spaces before its comment to match the
realigned column). Ran `clang-format -i` on all four of this task's files;
the only output change was that one whitespace-only comment-column shift in
`bk_app.h` — no include reordering, and no type, field, or function body
changed anywhere. Note the same conflict is not yet resolved upstream:
`include/bielik/bk_time.h` and `include/bielik/bk_types.h`, both merged in
the prior (Task 1) fundamental-types commit, still fail
`clang-format --dry-run --Werror` for the same reason and were left as-is
since they're outside this task's file list.

## PLAN.md §5's "4-space indent" is superseded by the expanded `.clang-format` (PLAN.md:78)

`PLAN.md` §5 describes the Phase 0 `.clang-format` as "LLVM base, 4-space indent, 100
columns, `PointerAlignment: Right` ... K&R attached braces." The clang-format
modernization effort expanded that 5-line stub into a fuller config and switched to
2-space indent, plus added `Language: C`, enforced include-block regrouping
(`IncludeBlocks: Regroup`), always-expanded function bodies
(`AllowShortFunctionsOnASingleLine: None`), and a handful of alignment/PP-directive
options — none of which PLAN.md:78 mentions. Per this file's own convention (see the
§6.1 entry above), `PLAN.md`'s text is left as the historical record of the Phase 0
config rather than rewritten to match; `.clang-format` itself, `CLAUDE.md`'s Style
section, and this entry are the source of truth for the current formatting rules.

This also resolves the drift the `bk_app.h trailing-comment column` entry above left
outstanding: `bk_time.h` and `bk_types.h` now pass `clang-format --dry-run --Werror`
along with the rest of the tree, and CI's new `format` job blocks on it going forward
— so the recurring per-task "clang-format overrode my hand-alignment" entries in this
file should stop appearing.

## `bk_math` ships header-only, with no `src/bk_math.c` (CLAUDE.md Conventions, docs/superpowers/specs/2026-08-01-bk-math-design.md §2)

`CLAUDE.md` states the module rule as "One module = `include/bielik/bk_<name>.h` +
`src/bk_<name>.c` (+ optional `src/internal/bk_<name>_internal.h`)". `bk_math` has no
`.c` file: every function is `static inline` in the header. Each one is between one and
eight arithmetic operations, and Phase 3's draw layer calls them in per-sprite,
per-vertex loops — the batch transform path is the hot loop of the whole framework, so a
translation-unit boundary would mean either eating the call overhead or depending on LTO
to undo it. `CLAUDE.md` also commits the project to writing SoA scalar code and checking
clang's autovectorization before reaching for SIMD, which only works if the arithmetic is
visible at the call site. Checked for an exception: the largest function is `bk_m3x2_inv`
(a 2x2 determinant, a reciprocal, four multiplies, and a transformed translation), still
well inside inlining range — a `.c` file holding one function would be worse than the
deviation. Two properties this depends on were verified empirically rather than assumed,
by compiling a two-translation-unit probe under the project's exact flags (`-std=c23
-Wno-everything -Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla -Werror`, AppleClang
21.0.0): (1) an unused `static inline` in an included header does not trip
`-Wunused-function`; (2) a file-scope `constexpr f32` in a header included by two TUs
does not collide at link time — taking its address in both yielded *different* pointers,
confirming C23 gives `constexpr` objects internal linkage (one copy per TU, like `static
const`), so `BK_PI` can be a `constexpr` as `CLAUDE.md` prefers for constants rather than
falling back to a macro. The root `CMakeLists.txt` needed no change at all, since
`target_include_directories(bielik PUBLIC include)` already exports the whole tree;
`tests/test_header_bk_math.c` still enforces standalone compilation like every other
public header.

## `BK_Color` moved from `bk_gfx.h` to `bk_math.h` (PLAN.md §6.1, docs/superpowers/specs/2026-08-01-bk-math-design.md §3)

`PLAN.md` §6.1 defines `BK_Color` in `include/bielik/bk_gfx.h`. It now lives in
`include/bielik/bk_math.h`, and `bk_gfx.h` includes that header. The move is right on the
merits rather than merely convenient: color arithmetic (premultiply, lerp, hex and rgba8
conversion) is math, and Phase 3's command format stores premultiplied color as two
`packHalf2x16` words, so those operations run per-command inside the batcher, not in the
gfx layer — leaving the type in `bk_gfx.h` while its operations lived in `bk_math.h`
would have been the worse split. Source compatibility was verified rather than assumed:
`grep -rn "BK_Color" include src samples tests` found 17 use sites across `bk_gfx.h`,
`src/bk_gfx.c`, `src/internal/bk_gfx_internal.h`, `samples/01_clear`, `samples/02_ticks`,
`tests/test_gfx.c`, and `tests/test_gfx_capture.c`, and every one already includes
`bk_gfx.h` directly or transitively. The type name is unchanged, so no use site needed
editing, and the full tree (library, all six samples, all 15 tests) builds clean under
`-DBK_WERROR=ON` after the move with no other edits.
