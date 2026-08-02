# gfx substrate Implementation Plan

**Goal:** Give `bk_gfx` the capabilities Phase 3's draw layer needs — a per-frame draw
list, storage-buffer and uniform binding, instanced draws, scissor/viewport, and the
blend modes CF uses — without changing how any existing sample calls it.

**Architecture:** No new module. `bk_gfx.c`'s single pending slot becomes a chain of
arena-allocated records; the bind functions keep their names and set "current state"; a
draw appends a snapshot. `bk_gfx_buffer` and `bk_gfx_pipeline` each gain one small
capability. Everything else in the tree is untouched.

**Tech Stack:** C23, CMake, clang-everywhere, SDL3/SDL_GPU, `-Wall -Wextra -Wshadow
-Wstrict-prototypes -Wvla -Werror`, glslc + spirv-cross for the sample's shaders.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-01-gfx-substrate-design.md` — read it first;
  this plan assumes its §3–§10 and does not restate the rationale.
- Prerequisite: `bk_math` (P3.1, merged in PR #20). `BK_Rect` is the scissor/viewport
  type and `bk_m3x2_ortho` builds the sample's camera.
- Use `bk_types.h` aliases throughout. No single-letter identifiers outside the
  `.clang-tidy` exempt set (`i`, `x`, `y`, `r`, `g`, `b`, `a`, `t`).
- Every task builds clean under
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
  and passes `ctest --test-dir build --output-on-failure`.
- Run `cmake --build build --target format-check` before each commit.
- TDD red-green. The one thing that cannot be tested first is Task 3's internal
  restructure — there, the existing suite staying green *is* the test, and the new
  behavior gets its tests in the same task.
- **The four samples must not be edited in any task.** If a task needs a sample changed,
  the call shape broke and the spec's §3 claim is wrong — stop and reassess rather than
  editing the sample.
- Atomic commits, human voice, imperative, no Conventional Commits prefixes.
- Tasks 1 and 2 are independent of each other and of the rest. Task 3 must land before
  4, 5, and 6.

---

## Task 1: buffer usage for graphics-stage storage reads

**Files:** `include/bielik/bk_gfx_buffer.h`, `src/bk_gfx_buffer.c`,
`samples/05_compute/main.c`, `tests/test_gfx_buffer.c`, `tests/test_gfx_compute.c`.

This is the one task that *does* touch a sample, and only to rename an identifier — no
call-shape change. Five references total, confirmed by
`grep -rn BK_GFX_BUFFER_USAGE_STORAGE_READ include src samples tests`.

- [ ] **Step 1 (red):** in `tests/test_gfx_buffer.c`, add a case creating a buffer with
      `BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS` and asserting it is non-null, then
      destroying it. Fails to compile — the enum value doesn't exist.

- [ ] **Step 2 (green):** rename `BK_GFX_BUFFER_USAGE_STORAGE_READ` to
      `BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE` and add the new value:

```c
typedef enum BK_GfxBufferUsage {
  BK_GFX_BUFFER_USAGE_VERTEX,
  BK_GFX_BUFFER_USAGE_INDEX,        // 16-bit indices; bk_gfx_bind_index_buffer hardcodes
                                    // the element size, see bk_gfx.h
  BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE,  // read-only storage buffer in a compute shader
  BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, // read-only storage buffer in a vertex and/or
                                        // fragment shader -- one SDL creation flag
                                        // covers both graphics stages; the stage
                                        // distinction is at bind time (see bk_gfx.h)
} BK_GfxBufferUsage;
```

and in `s_buffer_usage_flags`:

```c
  case BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE:
    return SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
  case BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS:
    return SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
```

- [ ] **Step 3:** update the three remaining references to the old name. `grep -rn
      BK_GFX_BUFFER_USAGE_STORAGE_READ include src samples tests` must return nothing.

**Commit:** `add a graphics-stage storage buffer usage`

---

## Task 2: pipeline storage-buffer counts and CF's blend modes

**Files:** `include/bielik/bk_gfx_pipeline.h`, `src/bk_gfx_pipeline.c`,
`tests/test_gfx_pipeline.c`.

- [ ] **Step 1 (red):** in `tests/test_gfx_pipeline.c`, add a case creating a pipeline
      with `BK_GFX_BLEND_PREMULTIPLIED` and asserting non-null. Fails to compile.

- [ ] **Step 2 (green):** add `i32 num_storage_buffers;` to `BK_GfxShaderDesc`, directly
      after `num_samplers`, and pass it through to
      `SDL_GPUShaderCreateInfo.num_storage_buffers` in `s_create_shader`. Doc comment:

```c
  i32 num_samplers;
  i32 num_storage_buffers; // storage buffers this stage reads (bk_gfx_bind_*_storage_buffer)
  i32 num_uniform_buffers;
```

Extend `BK_GfxShaderDesc`'s existing comment to note that `num_storage_buffers` is
subject to the same silent-failure rule as the other counts: a mismatch is not caught at
creation, it drops the whole command buffer at draw time with nothing logged.

- [ ] **Step 3 (green):** replace the `if (desc->blend_mode == BK_GFX_BLEND_ALPHA)` block
      with a switch. **`BK_GFX_BLEND_ALPHA`'s factors are unchanged** — verified at
      `src/bk_gfx_pipeline.c:166-175`, it is straight (non-premultiplied) alpha, and
      redefining it would alter already-shipped pipelines:

```c
typedef enum BK_GfxBlendMode {
  BK_GFX_BLEND_NONE,          // blending disabled
  BK_GFX_BLEND_ALPHA,         // SRC_ALPHA, ONE_MINUS_SRC_ALPHA -- straight alpha
  BK_GFX_BLEND_PREMULTIPLIED, // ONE, ONE_MINUS_SRC_ALPHA
  BK_GFX_BLEND_ADDITIVE,      // ONE, ONE                 -- glows, particles
  BK_GFX_BLEND_MULTIPLY,      // DST_COLOR, ZERO          -- shadows, tints
  BK_GFX_BLEND_SCREEN,        // ONE, ONE_MINUS_SRC_COLOR -- light accumulation
} BK_GfxBlendMode;
```

Implement as a helper returning the blend state, keeping `bk_gfx_pipeline_create` small:

```c
static SDL_GPUColorTargetBlendState s_blend_state(BK_GfxBlendMode mode) {
  SDL_GPUBlendFactor src = SDL_GPU_BLENDFACTOR_ONE;
  SDL_GPUBlendFactor dst = SDL_GPU_BLENDFACTOR_ZERO;
  switch (mode) {
  case BK_GFX_BLEND_NONE:
    return (SDL_GPUColorTargetBlendState){0};
  case BK_GFX_BLEND_ALPHA:
    src = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    break;
  case BK_GFX_BLEND_PREMULTIPLIED:
    src = SDL_GPU_BLENDFACTOR_ONE;
    dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    break;
  case BK_GFX_BLEND_ADDITIVE:
    src = SDL_GPU_BLENDFACTOR_ONE;
    dst = SDL_GPU_BLENDFACTOR_ONE;
    break;
  case BK_GFX_BLEND_MULTIPLY:
    src = SDL_GPU_BLENDFACTOR_DST_COLOR;
    dst = SDL_GPU_BLENDFACTOR_ZERO;
    break;
  case BK_GFX_BLEND_SCREEN:
    src = SDL_GPU_BLENDFACTOR_ONE;
    dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    break;
  }
  return (SDL_GPUColorTargetBlendState){
      .src_color_blendfactor = src,
      .dst_color_blendfactor = dst,
      .color_blend_op = SDL_GPU_BLENDOP_ADD,
      .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
      .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
      .enable_blend = true,
  };
}
```

No `default:` label — a switch over every enumerator lets `-Wswitch` catch a future mode
that forgets its factors, which a `default` would silently swallow.

Verify `BK_GFX_BLEND_ALPHA` still produces byte-identical state to the old code before
committing; `test_gfx_texture.c`'s golden-image assertions are the regression check.

**Commit:** `add storage buffer counts and CF's blend modes to pipelines`

---

## Task 3: the draw list

**Files:** `include/bielik/bk_gfx.h`, `src/bk_gfx.c`,
`src/internal/bk_gfx_internal.h`, `tests/test_gfx.c`, `tests/CMakeLists.txt`,
`tests/test_gfx_drawlist.c` (new).

The core of the sub-project. Everything else hangs off the record type defined here.

- [ ] **Step 1: define the record in `src/internal/bk_gfx_internal.h`.** The current-state
      struct and the record struct are deliberately the *same type*, so appending a draw
      is one struct copy:

```c
constexpr i32 BK_GFX_MAX_STORAGE_BUFFERS = 4;

/// One recorded draw: a snapshot of the bind state at the moment bk_gfx_draw* was
/// called, plus the draw's own counts. Allocated from the frame arena and chained in
/// call order; bk__gfx_flush walks the chain. Also used as the "currently bound state"
/// value that a draw snapshots from -- same shape, so recording is a struct copy.
typedef struct BK_GfxDrawCmd {
  BK_GfxPipeline *pipeline;
  BK_GfxBuffer *vertex_buffer;
  BK_GfxBuffer *index_buffer;
  BK_GfxTexture *texture;
  BK_GfxSampler *sampler;
  BK_GfxBuffer *vertex_storage[BK_GFX_MAX_STORAGE_BUFFERS];
  i32 num_vertex_storage; // highest bound slot + 1, so the bind is one contiguous run
  BK_GfxBuffer *fragment_storage[BK_GFX_MAX_STORAGE_BUFFERS];
  i32 num_fragment_storage;
  const void *vertex_uniform; // arena copy, not the caller's pointer; nullptr if unset
  u32 vertex_uniform_size;
  const void *fragment_uniform;
  u32 fragment_uniform_size;
  BK_Rect scissor;  // width/height <= 0 => full target
  BK_Rect viewport; // width/height <= 0 => full target
  i32 vertex_count;
  i32 index_count;
  i32 instance_count;
  struct BK_GfxDrawCmd *next;
} BK_GfxDrawCmd;
```

Replace the eight `bk__gfx_get_pending_*` declarations' two count accessors with:

```c
/// Test-only: number of draws recorded this frame (reset by flush).
i32 bk__gfx_get_draw_count(void);

/// Test-only: the index'th recorded draw in call order, or nullptr if out of range.
const BK_GfxDrawCmd *bk__gfx_get_draw_cmd(i32 index);
```

The six bind accessors keep their names and meaning ("currently bound state").

- [ ] **Step 2: restructure `src/bk_gfx.c`.** Replace the file-static pending slots with:

```c
static BK_GfxDrawCmd s_state;       // currently bound state
static BK_GfxDrawCmd *s_draw_head;  // first recorded draw this frame
static BK_GfxDrawCmd *s_draw_tail;
static i32 s_draw_count;
static BK_GfxCanvas *s_pending_canvas; // frame-level, NOT part of a record (spec §5)
```

`s_pending_canvas` deliberately stays its own static rather than a `BK_GfxDrawCmd` field:
canvas targeting is per-frame, and putting it on the record would imply per-draw
targeting the replay loop does not implement. Spec §5 describes what changes if that
decision is ever revisited.

Each bind writes into `s_state`; each draw appends:

```c
static void s_record_draw(i32 vertex_count, i32 index_count, i32 instance_count) {
  BK_GfxDrawCmd *cmd = bk_frame_alloc(sizeof *cmd, alignof(BK_GfxDrawCmd));
  if (cmd == nullptr) {
    // Arena exhaustion is already logged and asserted by bk_frame_alloc; dropping the
    // draw is better than dereferencing null, and the frame still presents.
    return;
  }
  *cmd = s_state;
  cmd->vertex_count = vertex_count;
  cmd->index_count = index_count;
  cmd->instance_count = instance_count;
  cmd->next = nullptr;
  if (s_draw_tail == nullptr) {
    s_draw_head = cmd;
  } else {
    s_draw_tail->next = cmd;
  }
  s_draw_tail = cmd;
  s_draw_count++;
}
```

`bk_gfx_draw(n)` calls `s_record_draw(n, 0, 1)`; `bk_gfx_draw_indexed(n)` calls
`s_record_draw(0, n, 1)`.

- [ ] **Step 3: snapshot and clear at the TOP of `bk__gfx_flush`.** Non-negotiable, and
      the reason is in spec §3: `bk_app.c:274-275` runs `bk__gfx_flush()` then
      `bk__arena_reset()` unconditionally, and flush early-returns on acquire failure and
      on a null swapchain texture (minimized window). Clearing at the end would leave the
      chain pointing into recycled arena memory — a heap-use-after-free that ASan catches
      only if a test minimizes the window. The existing pending-slot clear block already
      sits at the top for exactly this reason; the list joins it:

```c
  BK_GfxDrawCmd *draw_head = s_draw_head;
  BK_GfxCanvas *pending_canvas = s_pending_canvas;
  char pending_capture_path[sizeof s_pending_capture_path];
  SDL_memcpy(pending_capture_path, s_pending_capture_path, sizeof pending_capture_path);
  s_state = (BK_GfxDrawCmd){0};
  s_draw_head = nullptr;
  s_draw_tail = nullptr;
  s_draw_count = 0;
  s_pending_canvas = nullptr;
  s_pending_capture_path[0] = '\0';
```

- [ ] **Step 4: replay the chain inside the render pass.** Replaces the single
      `if (pending_pipeline != nullptr)` block. Bind only what changed between
      consecutive records, tracking the last-bound values in locals:

```c
  const BK_GfxPipeline *bound_pipeline = nullptr;
  for (const BK_GfxDrawCmd *cmd = draw_head; cmd != nullptr; cmd = cmd->next) {
    if (cmd->pipeline == nullptr) {
      continue; // a draw with no pipeline bound records nothing drawable
    }
    if (cmd->pipeline != bound_pipeline) {
      // Per-record, not per-frame: a frame mixing a depth-enabled and a depth-disabled
      // pipeline must keep this named diagnostic for both, not just the first.
      BK_ASSERT(bk__gfx_pipeline_depth_format(cmd->pipeline) == depth_target_format);
      SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(cmd->pipeline));
      bound_pipeline = cmd->pipeline;
    }
    ... vertex/index/texture binds, unchanged in shape from today ...
    if (cmd->index_count > 0) {
      SDL_DrawGPUIndexedPrimitives(pass, (Uint32)cmd->index_count,
                                   (Uint32)cmd->instance_count, 0, 0, 0);
    } else if (cmd->vertex_count > 0) {
      SDL_DrawGPUPrimitives(pass, (Uint32)cmd->vertex_count,
                            (Uint32)cmd->instance_count, 0, 0);
    }
  }
```

Keep the redundant-bind elision to the pipeline only for now — buffers and textures
rebind per record. Eliding those is a measurable-win optimization with no consumer yet
(`CLAUDE.md`: no speculative options), and correctness first makes the P3.3 batch's
behavior easier to reason about.

- [ ] **Step 5: update `bk_gfx.h`'s doc comments.** Every bind's "The binding is consumed
      (cleared) by the frame's flush" becomes "applies to every subsequent draw this
      frame". `bk_gfx_draw`/`_draw_indexed` gain "appends a draw record; the frame's
      flush replays records in call order". `bk_gfx_bind_canvas` keeps its consumed-at-
      flush wording — it really is frame-level.

- [ ] **Step 6: update `tests/test_gfx.c`.** The `bk__gfx_get_pending_vertex_count()` /
      `..._index_count()` assertions become `bk__gfx_get_draw_count()` and
      `bk__gfx_get_draw_cmd(0)->vertex_count` / `->index_count`. Nothing else in the file
      changes; the six bind accessors' assertions stand as-is.

- [ ] **Step 7 (the new tests): `tests/test_gfx_drawlist.c`**, registered in
      `tests/CMakeLists.txt` with `target_include_directories(... PRIVATE
      ${PROJECT_SOURCE_DIR}/src)` (it uses the internal header) and `bk_stage_shaders`.
      Cover, per spec §7:

  - **Ordering, both directions.** Two overlapping opaque quads; the second wins. Swap
    the order; the other wins. A one-direction test passes against a backwards replay.
  - **State is per-record, not last-wins.** Bind texture A, draw; bind texture B, draw.
    Each draw samples its own texture. This is the single most likely implementation bug.
  - **Record count and contents.** Three draws produce `bk__gfx_get_draw_count() == 3`
    with the expected per-record counts, and flush resets it to 0.
  - **Empty frame still clears and presents.**
  - **Per-record depth-format mismatch trips the assert** (second record's pipeline
    declares a different depth format).

  The pixel assertions use the existing offscreen + `bk__gfx_download_texture` pattern
  from `test_gfx_texture.c`, with hardcoded expected colors — never values derived from
  the same arithmetic the implementation uses.

**Commit:** `record draws into a per-frame list instead of one pending slot`

---

## Task 4: storage buffer binds and uniform pushes

**Files:** `include/bielik/bk_gfx.h`, `src/bk_gfx.c`, `tests/test_gfx_drawlist.c`.

- [ ] **Step 1 (red):** add tests. The uniform-lifetime one is the important one and is
      easy to write accidentally-passing — it must push from a stack buffer inside a
      helper that *returns* before flush, then overwrite that stack region:

```c
static void s_push_mvp_from_stack(f32 offset_x) {
  // Deliberately a local: bk_gfx_push_vertex_uniform must copy, not alias.
  f32 mvp[4] = {offset_x, 0.0f, 0.0f, 1.0f};
  bk_gfx_push_vertex_uniform(mvp, sizeof mvp);
}
```
      then call it, clobber the stack with an unrelated deep call, draw, flush, and
      assert the pixels reflect the pushed value.

- [ ] **Step 2 (green):** four functions, each writing `s_state`:

```c
void bk_gfx_bind_vertex_storage_buffer(BK_GfxBuffer *buffer, i32 slot) {
  BK_ASSERT(buffer != nullptr);
  BK_ASSERT(slot >= 0 && slot < BK_GFX_MAX_STORAGE_BUFFERS);
  s_state.vertex_storage[slot] = buffer;
  if (slot + 1 > s_state.num_vertex_storage) {
    s_state.num_vertex_storage = slot + 1;
  }
}

void bk_gfx_push_vertex_uniform(const void *data, u32 size) {
  BK_ASSERT(data != nullptr);
  BK_ASSERT(size > 0);
  void *copy = bk_frame_alloc(size, 0); // 0 => platform max alignment, per its contract
  if (copy == nullptr) {
    return; // bk_frame_alloc already logged/asserted
  }
  SDL_memcpy(copy, data, size);
  s_state.vertex_uniform = copy;
  s_state.vertex_uniform_size = size;
}
```
      plus the fragment equivalents. The arena copy is what makes the lifetime test pass:
      `SDL_PushGPU*UniformData` runs at flush, long after the caller's frame is gone.

- [ ] **Step 3 (green):** in the replay loop, before each draw:

```c
    if (cmd->num_vertex_storage > 0) {
      SDL_GPUBuffer *handles[BK_GFX_MAX_STORAGE_BUFFERS];
      for (i32 i = 0; i < cmd->num_vertex_storage; ++i) {
        handles[i] = cmd->vertex_storage[i] != nullptr
                         ? bk__gfx_buffer_handle(cmd->vertex_storage[i])
                         : nullptr;
      }
      SDL_BindGPUVertexStorageBuffers(pass, 0, handles, (Uint32)cmd->num_vertex_storage);
    }
    if (cmd->vertex_uniform != nullptr) {
      SDL_PushGPUVertexUniformData(cmd_buffer, 0, cmd->vertex_uniform,
                                   cmd->vertex_uniform_size);
    }
```

Note the uniform push targets the **command buffer**, not the render pass — that is
SDL's API shape, and it applies to subsequent draws on that command buffer.

**Commit:** `bind storage buffers and push uniforms per draw`

---

## Task 5: scissor, viewport, and instanced draws

**Files:** `include/bielik/bk_gfx.h`, `src/bk_gfx.c`, `tests/test_gfx_drawlist.c`.

- [ ] **Step 1 (red):** tests. Scissor: a full-target quad with a left-half scissor
      leaves the right half at the clear color; a zero rect resets to full. Instancing:
      `bk_gfx_draw_instanced(4, 3)` with per-instance offsets from a storage buffer puts
      three quads at three locations — assert a pixel inside each **and** one between
      them that must stay background, so a shader ignoring `gl_InstanceIndex` (three
      quads stacked at one spot) fails.

- [ ] **Step 2 (green):** `bk_gfx_set_scissor` / `bk_gfx_set_viewport` write `s_state`;
      `bk_gfx_draw_instanced(vertex_count, instance_count)` asserts both `> 0` and calls
      `s_record_draw(vertex_count, 0, instance_count)`.

- [ ] **Step 3 (green):** in the replay loop, per record. SDL has no "reset scissor"
      call, so the full-target rect is set explicitly rather than skipping the call —
      otherwise a record with no scissor would inherit the previous record's:

```c
    SDL_Rect scissor = {0, 0, (int)target_w, (int)target_h};
    if (cmd->scissor.width > 0 && cmd->scissor.height > 0) {
      scissor = (SDL_Rect){cmd->scissor.x, cmd->scissor.y, cmd->scissor.width,
                           cmd->scissor.height};
    }
    SDL_SetGPUScissor(pass, &scissor);
```
      and the analogous `SDL_GPUViewport` (which also carries `min_depth`/`max_depth` —
      use `0.0f`/`1.0f`). `target_w`/`target_h` are the already-computed canvas-or-
      swapchain dimensions, which is what makes the header's "pixels of the render
      target, not the window" doc accurate.

**Commit:** `add scissor, viewport, and instanced draws`

---

## Task 6: the `07_instanced` sample

**Files:** `shaders/instanced.vert`, `shaders/instanced.frag`, `CMakeLists.txt`,
`samples/07_instanced/{main.c,CMakeLists.txt}`, `samples/CMakeLists.txt`.

The end-to-end proof: N quads, per-instance data from a storage buffer, camera as a
pushed uniform built with `bk_m3x2_ortho`.

- [ ] **Step 1:** write the GLSL. Vertex shader reads a `std430 set=0 binding=0`
      storage buffer of per-instance `{vec2 position; vec2 half_size; vec4 color;}`,
      indexes it with `gl_InstanceIndex`, expands a unit quad from `gl_VertexIndex`
      (0..3, triangle strip), and applies a `set=1 binding=0` uniform MVP. Fragment
      shader outputs the interpolated color. Add
      `bk_compile_shader(NAME instanced STAGE vertex)` / `STAGE fragment` to the root
      `CMakeLists.txt`.

- [ ] **Step 2: verify MSL binding indices — the known silent-failure risk.** This is the
      first shader in the project combining a vertex-stage storage buffer *with* a
      uniform block, which is exactly where `spirv-cross`'s own MSL indexing can disagree
      with SDL_GPU's convention (uniform buffers first, then storage buffers; vertex
      buffers from `[[buffer(14)]]`):

```bash
grep -E '\[\[(texture|sampler|buffer)\(' shaders/instanced.vertex.msl \
                                          shaders/instanced.fragment.msl
```
      Check the emitted indices against that ordering. On a mismatch, fix with the
      `--msl-*-binding` remap flags chosen against the real output, add them to
      `cmake/shaders.cmake`, and record in `DEVIATIONS.md`. A wrong index here does not
      fail at pipeline creation — it drops the command buffer at draw time with an
      all-zero readback and nothing logged.

- [ ] **Step 3:** write `samples/07_instanced/main.c` following `04_textured_quad`'s
      structure (shader loading via `SDL_GetBasePath`, `BK_APP`, `--frames N`). Heavy
      comments — samples are documentation. Set `num_storage_buffers = 1` and
      `num_uniform_buffers = 1` on the vertex shader desc; a wrong count here is the
      same silent draw-time failure.

- [ ] **Step 4:** register in `samples/CMakeLists.txt`, and run it on a real desktop:
      the grid of quads must be visible and correctly positioned.

**Commit:** `add the 07_instanced sample`

---

## Final verification checklist

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
      clean, ASan/UBSan enabled.
- [ ] `ctest --test-dir build --output-on-failure` — all pass, including the pre-existing
      15 plus `test_gfx_drawlist`.
- [ ] **`git diff main..HEAD --stat -- samples/` shows only `05_compute` (one identifier,
      Task 1) and the new `07_instanced`.** Any other sample appearing means the call
      shape broke and spec §3 is wrong.
- [ ] **`git diff main..HEAD -- tests/test_gfx_pipeline.c tests/test_gfx_texture.c
      tests/test_gfx_canvas.c tests/test_gfx_capture.c tests/test_gfx_resize.c` is
      empty** apart from Task 2's added blend-mode case in `test_gfx_pipeline.c`.
- [ ] `cmake --build build --target format-check` passes.
- [ ] Release build compiles: `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
      -DBK_WERROR=ON && cmake --build build-release`.
- [ ] Every new public symbol doc-commented; `DEVIATIONS.md` has the draw-list semantics
      entry (§3) and any MSL remap flags from Task 6.
- [ ] **Mutation checks**, each reverted after: (a) replay the chain backwards — the
      ordering test must fail; (b) make records reference `s_state` instead of copying it
      — the per-record state test must fail; (c) make `bk_gfx_push_vertex_uniform` store
      the caller's pointer instead of an arena copy — the lifetime test must fail;
      (d) ignore `instance_count` (always 1) — the instancing test must fail. A suite
      that survives any of these is not testing what this task built.
- [ ] Minimize the window while `07_instanced` runs, under a Debug/ASan build, and
      confirm no use-after-free — the specific hazard Task 3 Step 3 exists to prevent,
      and the one path no automated test covers.
