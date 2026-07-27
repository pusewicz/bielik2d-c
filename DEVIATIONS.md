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
