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

## bk_task.h / bk_app.h / bk_task.c formatting (task-P1-5-brief.md)

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
