# Bielik2D — Codebase Structure

(Authoritative layout is PLAN.md §4 — never reorganize beyond it. Snapshot as of onboarding:)

```
CMakeLists.txt          top-level build: fetches SDL3, defines `bielik` static lib target
cmake/                  warnings.cmake, shaders.cmake (glslc/spirv-cross pipeline),
                         embed_shader.cmake, format.cmake
include/bielik/         public headers — one per module, e.g. bk_app.h, bk_draw.h,
                         bk_gfx.h, bk_gfx_buffer.h, bk_gfx_canvas.h, bk_gfx_pipeline.h,
                         bk_gfx_texture.h, bk_main.h, bk_math.h, bk_task.h, bk_time.h,
                         bk_types.h (fixed-width numeric aliases: i8..i64, u8..u64,
                         f32/f64, usize/isize, b32)
src/                     bk_app.c, bk_atlas.c, bk_draw.c, bk_gfx.c, bk_gfx_buffer.c,
                         bk_gfx_canvas.c, bk_gfx_pipeline.c, bk_gfx_texture.c, bk_task.c,
                         bk_time.c
src/internal/            bk_<name>_internal.h per module needing private state
                         (bk_app, bk_atlas, bk_draw, bk_gfx*, bk_task) — bk_atlas is
                         internal-only so far (bk__atlas_* symbols), no public caller
                         yet; P3.5 wires it up.
shaders/                 GLSL sources (.vert/.frag/.comp) + compiled .spv/.msl per shader
                         (triangle, textured, gradient, depth_tri, instanced, draw)
samples/                 one numbered sample per capability: 01_clear, 02_ticks,
                         03_triangle, 04_textured_quad, 05_compute, 06_canvas,
                         07_instanced, 08_draw
tests/                   bk_test.h (REQUIRE_SURFACE/REQUIRE_PIXEL helpers on SDL_test),
                         test_<module>.c per module, test_header_bk_<name>.c per public
                         header (standalone-compile check), test_draw_gpu.c (GPU-only
                         probes, see DEVIATIONS.md)
docs/superpowers/        specs/ (design docs) and plans/ (implementation plans) for each
                         Phase 2+ sub-project
PLAN.md                  normative spec: public API, module design, directory layout (§4),
                         build/test system (§5/§6), Phase 3 scope (§7)
DEVIATIONS.md            every deviation from PLAN.md / task briefs, with rationale
THOUGHTS.md              working design log — locked decisions + open questions
CLAUDE.md                agent-facing project instructions (this is the authoritative,
                         most up-to-date source — prefer it over this memory snapshot
                         if they ever disagree)
.worktrees/              git worktrees for isolated feature work (gitignored)
```

## Module status snapshot (see project_purpose memory for phase detail)
Landed: bk_app (Phase 1 core), bk_gfx + bk_gfx_buffer/canvas/pipeline/texture (Phase 2),
bk_math (header-only, P3.1), bk_draw (P3.3, unified SDF renderer), bk_atlas (P3.4,
internal residency cache, no public caller yet). Next: P3.5 (BK_Sprite/animation/
bk_draw_sprite/9-slice), then P3.6 (wide shapes), P3.7 (tiled compute path).
