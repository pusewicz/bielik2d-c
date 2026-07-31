# Phase 2 Sub-project 3: Canvases / Render Targets + Resize Handling + Depth-Stencil — Implementation Plan

> Retrospective record: this plan was executed task-by-task (superpowers:executing-plans
> shape, TDD red-green, one commit per task) on the `gfx-canvas` branch; every step
> below is checked off as actually done, not proposed.

**Goal:** Add a `bk_gfx_canvas` module (offscreen render targets), depth-stencil state
on `bk_gfx_pipeline`, an opt-in framework-owned swapchain depth texture
(`BK_WindowDesc.depth_stencil`), and window-resize handling (`bk_window_size`) — the
last slice of Phase 2 per `PLAN.md` §7.

**Architecture:** Canvases redirect the frame's existing single render pass rather
than adopting an always-render-offscreen model, so every pre-existing sample and
`bk_gfx_request_capture`'s contract stay untouched when no canvas is bound.
Depth-stencil is opt-in per pipeline (`BK_GfxPipelineDesc.depth_stencil_format`), not
derived from a bound canvas — Bielik2D's pipelines are built eagerly, not from a
format-keyed cache the way the donor code's are. Full rationale and the public API in
full is in the spec:
`docs/superpowers/specs/2026-07-31-canvas-render-targets-design.md` — read it before
touching this area again, this plan implements it exactly.

**Tech Stack:** C23, SDL3 GPU API, GLSL (`glslc`/`spirv-cross`), CMake, CTest.

## Global Constraints

- Spec of record: `docs/superpowers/specs/2026-07-31-canvas-render-targets-design.md`.
- Naming: public functions `bk_` + snake_case, public types `BK_` + PascalCase, enum
  values `BK_` + UPPER_SNAKE, internal linker-visible symbols `bk__` prefix,
  file-static functions `s_` prefix.
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).
- `.clang-format`: LLVM base, 4-space indent, 100 columns, `PointerAlignment: Right`,
  K&R attached braces. Run `clang-format -i` on every `.c`/`.h` file touched before
  committing — **not** on `CMakeLists.txt`/`.yml`, which clang-format will mangle if
  pointed at them by a blanket glob.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant.
- Errors: recoverable runtime failures (bad dimensions, SDL_GPU failure) log via
  `SDL_Log` with a `"BK: "` prefix and return `nullptr`/`false` — they do NOT assert.
  Programmer-error preconditions (null arguments, mismatched depth format) use
  `BK_ASSERT`.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`.
  Test: `ctest --test-dir build --output-on-failure`.
- Commit style: atomic commits, human-voice messages (no Conventional Commits
  prefixes, no AI signoff/Co-Authored-By footers).
- `#embed` is reserved for later phases — do not use it.

---

## Task 1: Texture usages + depth format probe

**Files:** `include/bielik/bk_gfx_canvas.h` (new), `src/bk_gfx_canvas.c` (new),
`include/bielik/bk_gfx_texture.h`, `src/bk_gfx_texture.c`,
`src/internal/bk_gfx_texture_internal.h`, `tests/test_gfx_texture.c`, `CMakeLists.txt`.

- [x] Add `BK_GFX_TEXTURE_USAGE_RENDER_TARGET` (`COLOR_TARGET | SAMPLER`) and
  `BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL` (`DEPTH_STENCIL_TARGET`, no sampler bit) to
  `BK_GfxTextureUsage`; depth usage's format comes from the new probe, not the
  hardcoded `R8G8B8A8_UNORM`.
- [x] Add `bk_gfx_depth_stencil_format(device)` to `bk_gfx_canvas.h`/`.c`: probes
  `D24_UNORM_S8_UINT` → `D32_FLOAT_S8_UINT` → falls back to `D16_UNORM`, via
  `SDL_GPUTextureSupportsFormat`.
- [x] Add `bk__gfx_texture_format(texture)` internal accessor.
- [x] Tests: both new usages create successfully; the probe returns a real depth
  format and a texture created at it round-trips through `bk__gfx_texture_format`.
- [x] Wire `src/bk_gfx_canvas.c` into `add_library(bielik STATIC ...)`.

## Task 2: `bk_gfx_canvas` module

**Files:** `include/bielik/bk_gfx_canvas.h`, `src/bk_gfx_canvas.c`,
`src/internal/bk_gfx_canvas_internal.h` (new), `tests/test_gfx_canvas.c` (new),
`tests/test_header_bk_gfx_canvas.c` (new), `tests/CMakeLists.txt`.

- [x] `BK_GfxCanvas`, `BK_GfxCanvasDesc` (`width`, `height`, `depth_stencil`,
  `blit_filter`), `bk_gfx_canvas_create`/`destroy`/`texture`/`size`, composed from two
  `bk_gfx_texture_create` calls (no duplicated SDL_GPU texture-creation logic).
- [x] Internal accessors: `bk__gfx_canvas_depth_handle`, `bk__gfx_canvas_depth_format`,
  `bk__gfx_canvas_blit_filter`.
- [x] Tests: with/without depth; `create` returns `nullptr` on non-positive
  dimensions; `destroy(nullptr)` no-op.

## Task 3: Pipeline depth state

**Files:** `include/bielik/bk_gfx_pipeline.h`, `src/bk_gfx_pipeline.c`,
`src/internal/bk_gfx_pipeline_internal.h`, `shaders/depth_tri.{vert,frag}` (new, +
committed `.spv`/`.msl`), `CMakeLists.txt`, `tests/test_gfx_canvas.c`,
`tests/CMakeLists.txt`.

- [x] `BK_GfxCompare` (`ALWAYS = 0` first), `depth_stencil_format`/`depth_compare`/
  `depth_write` on `BK_GfxPipelineDesc`, mapped onto
  `SDL_GPUGraphicsPipelineCreateInfo.target_info`/`.depth_stencil_state`. Test enabled
  by `depth_write || depth_compare != ALWAYS`, not `depth_write` alone.
- [x] `bk__gfx_pipeline_depth_format(pipeline)` internal accessor.
- [x] New shader pair `depth_tri` (`FLOAT3` position + `UBYTE4_NORM` color,
  passthrough fragment); MSL binding gate (`grep -E '\[\[(texture|sampler|buffer)\('
  shaders/depth_tri.*.msl`) confirmed empty, as expected for a shader with no samplers
  or uniform buffers.
- [x] Golden-image test in `tests/test_gfx_canvas.c`, built against a real
  `BK_GfxCanvas` (not a hand-rolled texture): two canvas-covering triangles at
  different depths, run with draw order swapped, near triangle wins both times.
  Verified the test actually discriminates by temporarily breaking `depth_compare` to
  `ALWAYS`, confirming failure, then restoring it.

## Task 4: `bk_gfx_bind_canvas` + flush restructure

**Files:** `include/bielik/bk_gfx.h`, `src/bk_gfx.c`, `src/internal/bk_gfx_internal.h`,
`tests/test_gfx.c`.

- [x] `bk_gfx_bind_canvas(canvas)` (nullptr-able, unlike other `bk_gfx_bind_*` — it's
  the "go back to the swapchain" affordance), `bk__gfx_get_pending_canvas` test-only
  accessor.
- [x] `bk__gfx_flush` restructure: target selection (canvas's attachments vs. the
  swapchain), `SDL_GPUDepthStencilTargetInfo *` that's `nullptr` when there's no
  attachment, `SDL_BlitGPUTexture` onto the swapchain when a canvas was bound
  (`load_op = CLEAR`, `cycle = true`), `BK_ASSERT` that a bound pipeline's declared
  depth format matches the pass's actual attachment.
- [x] Verified additive: `test_gfx_capture` (real windowed run, no canvas bound) still
  passes unchanged.

## Task 5: `samples/06_canvas` — review checkpoint

**Files:** `samples/06_canvas/{CMakeLists.txt,main.c}` (new), `samples/CMakeLists.txt`.

- [x] Two overlapping triangles (near red z=0.25, far blue z=0.75, drawn far-first on
  purpose) into a 160×90 canvas with depth, `NEAREST`-blitted onto a resizable
  960×540 window. `--frames N` for CI smoke; dual `06_canvas`/`06_canvas_run` targets
  matching every other sample.
- [x] Verified visually via a temporary (not committed) `bk_gfx_request_capture` call:
  red correctly occludes blue in the overlap region, upscale is crisply blocky.
- [x] Re-ran `03_triangle`/`04_textured_quad`/`05_compute` to confirm the sub-project
  stayed additive up to this point.

## Task 6: Swapchain depth opt-in

**Files:** `include/bielik/bk_app.h`, `src/bk_app.c`, `src/bk_gfx.c`,
`src/internal/bk_gfx_internal.h`.

- [x] `BK_WindowDesc.depth_stencil`. `bk__gfx_configure_swapchain_depth(enabled)`
  called once from `bk__boot` after the GPU device exists.
- [x] File-static swapchain depth texture in `bk_gfx.c`, created lazily on first
  flush, recreated whenever `swap_w`/`swap_h` no longer match its tracked size — no
  `SDL_WaitForGPUIdle` needed (SDL_GPU defers destruction past in-flight command
  buffers).
- [x] `bk__gfx_shutdown()`, called from `bk__shutdown` **before** `SDL_DestroyGPUDevice`.
- [x] `bk__gfx_get_swapchain_depth_size` test-only accessor (consumed by task 7's
  test).
- [x] Verified end-to-end via a scratch (not committed) edit to
  `test_app_lifecycle.c`: real 64×64 window, `depth_stencil = true`, confirmed the
  texture came out 64×64 after the first flush; reverted before committing.

## Task 7: Resize handling

**Files:** `include/bielik/bk_app.h`, `src/bk_app.c`, `tests/test_gfx_resize.c` (new),
`tests/CMakeLists.txt`, `samples/06_canvas/main.c`.

- [x] `bk_window_size(i32 *out_w, i32 *out_h)`, cached in `BK_AppState`, seeded at
  boot, refreshed on `SDL_EVENT_WINDOW_RESIZED`/`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
  (both, idempotently) at the top of `bk__event`, before any app `.event` handler —
  and without consuming the event.
- [x] `tests/test_gfx_resize.c`: real window via `bk_run`, `SDL_SetWindowSize`
  mid-run, then (from inside `test_update`, since `bk_window()`/`bk_gpu()` are invalid
  after `bk_run` returns) asserts `bk_window_size` agrees with SDL's own query and the
  framework depth texture matches both. `TIMEOUT 30`, matching
  `test_app_lifecycle`/`test_gfx_capture`. Ran 5x locally to check for flakiness —
  none observed.
- [x] `samples/06_canvas`'s resize log switched from a raw `SDL_GetWindowSizeInPixels`
  call (used as a stand-in since task 5 predates this task) to `bk_window_size`, so
  the new public API has a real sample consumer, not just a test.

## Task 8: CI wiring, docs, whole-branch verification

**Files:** `.github/workflows/ci.yml`, `CLAUDE.md`,
`docs/superpowers/specs/2026-07-31-canvas-render-targets-design.md` (new, this doc's
sibling), this plan doc.

- [x] Added `test_gfx_canvas`/`test_gfx_resize` to **all six** occurrences of the
  GPU-dependent test regex in `ci.yml` (one `-E` exclusion + two `-R` inclusions,
  across the `build-and-test` and `debug-sanitizers` jobs — no CTest label exists to
  make this automatic).
- [x] `CLAUDE.md`: Phase 2 marked complete, Phase 3 (draw2d) named as next.
- [x] `DEVIATIONS.md`: checked — nothing diverged from this spec beyond what's already
  documented in code comments; no entry needed.
- [x] Full clean build + test suite, Debug and Release, both `-DBK_WERROR=ON`; every
  sample smoke-tested with `--frames N`; `06_canvas` additionally verified visually.
