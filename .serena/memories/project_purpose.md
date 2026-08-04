# Bielik2D — Project Purpose

Bielik2D is a 2D game framework written in C23, designed to be lightweight, fast, and
easy to use. It targets both beginners and experienced developers.

## Relationship to Cute Framework (CF)
Bielik2D is a from-scratch, clean rebuild that treats Cute Framework as **donor code**,
not a dependency: CF is C++ so a C23 port can't just wrap it, but its zlib/public-domain
license means transliterating its proven pieces (SDF batcher/shaders, draw API semantics,
GLES3 backend) beats reinventing them. A CF checkout for reference lives at
`/Users/piotr/Work/GitHub/pusewicz/cute_framework`.

## v1 finish line
Space Delivery (the user's existing game) runs on Bielik2D, feature-identical to its
current Cute Framework build. That port is the scope boundary for v1.

## Current status (see CLAUDE.md for authoritative up-to-date detail)
- Phase 0 (scaffold) and Phase 1 (app core): implemented.
- Phase 2 (gfx core): complete, landed as 3 sub-projects with specs/plans under
  `docs/superpowers/specs/` and `docs/superpowers/plans/`.
- Phase 3 (draw2d): 7 sub-projects (P3.1–P3.7). Landed: P3.1 `bk_math`, P3.2 gfx
  substrate, P3.3 `bk_draw` (unified SDF renderer), P3.4 `bk_atlas` (runtime residency
  cache, internal-only, no caller yet — P3.5 wires it up). Remaining: P3.5 (`BK_Sprite`,
  animation, `bk_draw_sprite`, 9-slice), P3.6 (wide shapes), P3.7 (tiled compute path).

## Key docs (read before major work)
- `PLAN.md` — normative spec for public API and module design (§6.1 esp.); §4 has the
  current directory layout; §5/§6 describe the build system and test suite; §7 covers
  Phase 3's overall scope.
- `DEVIATIONS.md` — every place implementation deviated from PLAN.md or a task brief,
  with rationale. Must be checked and added to whenever knowingly diverging.
- `THOUGHTS.md` — working design log; primary source of architectural intent ahead of
  real code/headers. Mix of locked decisions and open questions (see tech_stack memory).
- `docs/superpowers/specs/2026-08-03-bk-atlas-design.md` §0 — current sub-project table
  for Phase 3 (supersedes the out-of-date table in the bk-draw-design spec's §0).
