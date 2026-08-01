# Bielik2D — Phase 3, Sub-project 2: gfx substrate

## 0. Context and scope

Phase 2 closed with a complete gfx core: pipelines, buffers, textures, compute,
canvases, depth-stencil, resize. Phase 3 ports Cute Framework's tiled-compute command
renderer (`src/cute_draw.cpp`, `tools/builtin_shaders.h` in the CF checkout), and that
renderer needs capabilities `bk_gfx` does not yet have. This sub-project adds them. It
ships no draw API of its own — `bk_draw.h` is P3.3.

Two things make this the riskiest slice of Phase 3, and both are decided here rather
than discovered later:

- It **breaks `bk_gfx.h`'s single-pending-slot model**, which four samples and four
  tests use.
- It fixes the **canvas targeting granularity** for everything built on top (§5).

P3.1 (`bk_math`) landed first and is a prerequisite: `BK_Rect` is this module's scissor
and viewport type.

## 1. What the renderer needs, and what exists today

Derived by reading CF's `s_inst_vs`, `s_tile_fs`, `s_tile_zero_cs`, `s_tile_scan_cs`:

| Renderer needs | Today | This sub-project |
|---|---|---|
| Storage buffers read by the **vertex** stage (`cmds`, `payload`) | `BK_GFX_BUFFER_USAGE_STORAGE_READ` maps to compute only | new buffer usage + `bk_gfx_bind_vertex_storage_buffer` |
| Storage buffers read by the **fragment** stage (tiled path) | none | `bk_gfx_bind_fragment_storage_buffer` |
| Uniform blocks (MVP, canvas size, tile params) | `num_uniform_buffers` exists; nothing pushes data | `bk_gfx_push_{vertex,fragment}_uniform` |
| Instanced draw (`gl_InstanceIndex`) | instance count hardcoded to 1 | `bk_gfx_draw_instanced` |
| Many ordered draws per frame, state changing between | one pending slot, one draw | per-frame draw list (§4) |
| Scissor + viewport per run | none | `bk_gfx_set_scissor` / `_viewport` |
| Additive / multiply / screen blending | none/alpha only | three new `BK_GfxBlendMode` values |

**Deliberately deferred to P3.6** (the tiled compute path), because nothing before it has
a consumer: compute dispatch inside the frame's command buffer, read-write storage
*buffers*, and per-frame streaming/cycled upload. `bk_gfx_compute_dispatch`'s existing
synchronous, own-command-buffer form is sufficient for P3.3–P3.5.

**One SDL detail collapses two rows into one.** `SDL_BindGPUVertexStorageBuffers` and
`SDL_BindGPUFragmentStorageBuffers` both document their buffers as requiring
`SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ` — a single creation flag covers both
graphics stages. So one new enum value serves both, and the vertex/fragment split exists
only at bind time.

## 2. File layout

```
bielik2d/
  include/bielik/
    bk_gfx.h                 (modified: draw list, storage/uniform binds, scissor/viewport)
    bk_gfx_buffer.h          (modified: graphics-storage usage)
    bk_gfx_pipeline.h        (modified: num_storage_buffers, blend modes)
  src/
    bk_gfx.c                 (modified: the draw list and its replay)
    bk_gfx_buffer.c          (modified: usage mapping)
    bk_gfx_pipeline.c        (modified: shader resource counts, blend state)
  shaders/
    instanced.vert / .frag   (new; the acceptance sample's, not a draft of P3.3's inst_vs)
  samples/
    07_instanced/            (new)
  tests/
    test_gfx_drawlist.c      (new)
```

No new module. Everything here extends existing ones, so `PLAN.md` §4's layout rule
holds.

## 3. Decision: the pending slot becomes a draw list, with the same call shape

Today every `bk_gfx_bind_*` writes a file-static slot and `bk__gfx_flush` performs at
most one draw. The new model:

- **Binds set current state.** Sticky within a frame, cleared at flush — same as now.
- **A draw call appends a record** capturing a snapshot of that state, then the frame
  can keep binding and drawing.
- **Flush replays the records in order**, setting only what changed between consecutive
  records.

The existing entry points keep their names and signatures. `03_triangle`,
`04_textured_quad`, `05_compute`, and `06_canvas` all follow bind…bind…draw-once, so
they behave identically and **need no edits** — verified by their tests continuing to
pass unchanged. What changes is the documented contract: "the binding is consumed by the
frame's flush" becomes "the binding applies to every subsequent draw this frame."

This is an API semantic change, not a signature change, and gets a `DEVIATIONS.md`
entry. `bk_draw_*` (P3.3) sits on top of this rather than replacing it — a game can drop
to raw pipelines for a custom effect without leaving the framework.

**Records live in the frame arena.** `bk_frame_alloc` (4 MiB, doubling, reset after
flush, usable from `init`) is exactly the right allocator and already exists — do not
hand-roll one. This also settles uniform-data lifetime: `SDL_PushGPU*UniformData` runs at
flush against the command buffer, so a record must own a *copy* of the caller's uniform
bytes, not a pointer into caller memory. That copy is an arena allocation.

The list is a singly-linked chain of arena-allocated records rather than a growable
array: the arena cannot realloc in place, and a chain avoids copying records when the
frame's draw count is unknown up front.

**The list head/tail must be snapshotted and cleared at the very top of
`bk__gfx_flush`, before the command-buffer acquire — not at the end.** This is not
stylistic. `bk_app.c:274-275` runs `bk__gfx_flush()` then `bk__arena_reset()`
unconditionally, and flush has two early returns before it draws anything: a failed
`SDL_AcquireGPUCommandBuffer`, and a null swapchain texture (minimized or occluded
window). If the list were cleared at the end, either path would leave head/tail pointing
into arena memory that `bk__arena_reset` then recycles — and if the arena grew via
`bk__realloc` in the meantime, the block moved and those are freed pointers. That is an
ASan heap-use-after-free on the minimize-the-window path, which no current test
exercises. Clearing at the top is also exactly what the existing code already does with
its pending slots, for the same reason; the list simply joins that block.

## 4. Public API — `bk_gfx.h`

```c
#pragma once
#include <bielik/bk_math.h> // BK_Color, BK_Rect
#include <bielik/bk_types.h>

typedef struct BK_GfxPipeline BK_GfxPipeline;
typedef struct BK_GfxBuffer BK_GfxBuffer;
typedef struct BK_GfxTexture BK_GfxTexture;
typedef struct BK_GfxSampler BK_GfxSampler;
typedef struct BK_GfxCanvas BK_GfxCanvas;

/// Sets the color the swapchain is cleared to each frame.
void bk_gfx_set_clear_color(BK_Color color);

// --- State. Sticky: applies to every subsequent draw this frame, until changed or
// until the frame's flush resets it. ---

/// Binds a pipeline for subsequent draws this frame.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Binds a vertex buffer to slot 0 for subsequent draws this frame.
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);

/// Binds an index buffer for subsequent indexed draws this frame. Indices are always
/// read as 16-bit (see bk_gfx_buffer.h's BK_GFX_BUFFER_USAGE_INDEX).
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);

/// Binds a texture and sampler pair to fragment slot 0 for subsequent draws this frame.
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);

/// Binds a storage buffer the vertex shader reads, at the given slot. buffer must have
/// been created with BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS. Max BK_GFX_MAX_STORAGE_BUFFERS
/// slots; the pipeline's vertex shader desc must declare a matching num_storage_buffers.
void bk_gfx_bind_vertex_storage_buffer(BK_GfxBuffer *buffer, i32 slot);

/// Binds a storage buffer the fragment shader reads, at the given slot. Same
/// requirements as the vertex form.
void bk_gfx_bind_fragment_storage_buffer(BK_GfxBuffer *buffer, i32 slot);

/// Copies size bytes into the frame arena as vertex uniform slot 0 for subsequent draws
/// this frame. The data is copied, so a stack struct is fine. Must respect std140
/// layout (vec3/vec4 members 16-byte aligned) -- SDL_GPU pushes it verbatim. The
/// pipeline's vertex shader desc must declare num_uniform_buffers >= 1.
void bk_gfx_push_vertex_uniform(const void *data, u32 size);

/// Fragment-stage equivalent of bk_gfx_push_vertex_uniform, at fragment uniform slot 0.
void bk_gfx_push_fragment_uniform(const void *data, u32 size);

/// Restricts subsequent draws this frame to rect, in pixels of the render target with a
/// top-left origin -- the bound canvas's dimensions when one is bound, the swapchain's
/// otherwise, NOT the window's. A width or height <= 0 means "no scissor" (the full
/// target), which is also the default, so passing a zero rect resets it.
void bk_gfx_set_scissor(BK_Rect rect);

/// Maps subsequent draws this frame onto rect, in pixels of the render target with a
/// top-left origin -- same canvas-relative meaning as bk_gfx_set_scissor. A width or
/// height <= 0 means "full target", which is also the default.
void bk_gfx_set_viewport(BK_Rect rect);

/// Renders this frame into canvas instead of the swapchain, then blits the result onto
/// the swapchain once the pass ends. Frame-level, not per-draw: the whole frame targets
/// one canvas (see the design spec's §5). Consumed at flush; pass nullptr (or don't
/// call it) for the swapchain.
void bk_gfx_bind_canvas(BK_GfxCanvas *canvas);

// --- Draws. Each appends a record capturing the state bound above; the frame's flush
// replays them in call order. ---

/// Draws vertex_count vertices with the currently bound state.
void bk_gfx_draw(i32 vertex_count);

/// Draws index_count indices with the currently bound state, using the bound index
/// buffer.
void bk_gfx_draw_indexed(i32 index_count);

/// Draws instance_count instances of vertex_count vertices. The vertex shader reads
/// gl_InstanceIndex (SV_InstanceID) to select per-instance data, typically from a bound
/// vertex storage buffer -- this is the shape the P3.3 sprite batch is built on.
void bk_gfx_draw_instanced(i32 vertex_count, i32 instance_count);

/// Requests that this frame be saved as a BMP to path once presented. (unchanged)
void bk_gfx_request_capture(const char *path);
```

`BK_GFX_MAX_STORAGE_BUFFERS` is a `constexpr i32` of 4 — CF's tiled path binds four
fragment storage buffers (cmds, payload, tiles, list), which is the real upper bound in
Phase 3, and the instanced path binds two.

There is deliberately **no** `bk_gfx_draw_indexed_instanced`: CF's instanced path draws a
non-indexed coverage quad, and nothing else in P3 needs the combination. Add it when a
consumer appears.

## 5. Decision: canvas stays frame-level, per-draw targeting deferred

PR #18 made canvas a **frame-level** redirect: `bk_gfx_bind_canvas` retargets the frame's
single render pass and blits onto the swapchain. CF carries canvas targeting **per
command** (`CF_Command.is_canvas`/`.canvas`/`canvas_verts`, `cf_render_to()`), so one CF
frame can render into canvas A, then draw A as an item into canvas B.

**This sub-project keeps the frame-level model**, and the draw list is deliberately
shaped to match: one canvas per flush, not a field on each record.

Rationale: nothing in P3.2–P3.5 needs per-draw targeting. The sprite batch, SDF shapes,
and atlas all render into whatever target the frame is aimed at. Adding a per-record
canvas field now would mean a render pass per canvas switch, pass-ordering rules, and a
blit graph — real machinery with no consumer, which is exactly the speculative option
`CLAUDE.md` forbids.

The cost of being wrong is bounded and stated here so it is a decision rather than an
accident: adding per-record targeting later means moving the canvas from flush state onto
`BK_GfxDrawCmd` and splitting replay into one pass per contiguous same-target run. That
is a contained change to `bk__gfx_flush`, not an API break — `bk_gfx_bind_canvas`'s
signature already accommodates it. The thing that would be expensive is discovering it
during P3.3; hence this section.

**Obligation carried forward from P3.1's review:** the draw layer must not build an
inverse MVP from a singular transform (a sprite scaled to zero). `bk_m3x2_inv` asserts in
Debug, but Release compiles that out and a NaN uniform reaching the shader is undefined
fragment output with nothing logged. P3.3 guards at record time. Filed as
[bielik2d-c#21](https://github.com/pusewicz/bielik2d-c/issues/21) so it does not depend
on this paragraph being read.

## 6. Public API — `bk_gfx_buffer.h` and `bk_gfx_pipeline.h`

`BK_GfxBufferUsage` gains one value, and the existing compute one is renamed for
symmetry. Five references, verified by grep: the enum itself, its mapping in
`src/bk_gfx_buffer.c`, `samples/05_compute/main.c`, `tests/test_gfx_buffer.c`, and
`tests/test_gfx_compute.c`:

```c
typedef enum BK_GfxBufferUsage {
  BK_GFX_BUFFER_USAGE_VERTEX,
  BK_GFX_BUFFER_USAGE_INDEX,
  BK_GFX_BUFFER_USAGE_STORAGE_COMPUTE,  // was BK_GFX_BUFFER_USAGE_STORAGE_READ
  BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, // read by vertex and/or fragment shaders
} BK_GfxBufferUsage;
```

The usage stays **exclusive, not a bitmask**, per sub-project 2's decision. Known future
pressure, recorded rather than pre-solved: P3.6's tiled path wants buffers that are both
compute-written and fragment-read, which SDL permits as a flag combination. Revisit then,
with a real consumer, rather than speculatively widening now.

`BK_GfxShaderDesc` gains `i32 num_storage_buffers`, mirroring
`SDL_GPUShaderCreateInfo.num_storage_buffers`. This is the resource-count field
`CLAUDE.md`'s SDL_GPU gotcha is about: a mismatch does not fail at shader or pipeline
creation, it silently drops the whole command buffer at draw time, and the symptom is an
all-zero readback with nothing logged. The gotchas list already says to check these
first; this widens what "these" covers.

`BK_GfxBlendMode` gains four values. Each documents its exact SDL blend factors rather
than a prose label, because "alpha blending" is ambiguous about premultiplication and
that ambiguity is precisely what would bite:

```c
typedef enum BK_GfxBlendMode {
  BK_GFX_BLEND_NONE,          // blending off
  BK_GFX_BLEND_ALPHA,         // SRC_ALPHA, ONE_MINUS_SRC_ALPHA -- straight alpha
  BK_GFX_BLEND_PREMULTIPLIED, // ONE, ONE_MINUS_SRC_ALPHA
  BK_GFX_BLEND_ADDITIVE,      // ONE, ONE                 -- glows, particles
  BK_GFX_BLEND_MULTIPLY,      // DST_COLOR, ZERO          -- shadows, tints
  BK_GFX_BLEND_SCREEN,        // ONE, ONE_MINUS_SRC_COLOR -- light accumulation
} BK_GfxBlendMode;
```

**`BK_GFX_BLEND_ALPHA` keeps its existing factors**, verified against
`src/bk_gfx_pipeline.c:166-175`: it maps to `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` today,
i.e. *straight* (non-premultiplied) alpha. The premultiplied form the draw layer wants
arrives as a **new** mode rather than by redefining the shipped one — redefining it
would change the blend math for already-shipped pipelines and quietly invalidate any
golden-image expectation that samples a blended pixel, contradicting §3's and §7's
compatibility claims. The four new modes assume a premultiplied source, which is what
`bk_color_premultiply` produces and what P3.3's command format stores; `BK_GFX_BLEND_ALPHA`
remains for callers feeding straight-alpha colors.

## 7. Testing

`tests/test_gfx_drawlist.c`, following the golden-image pattern the existing gfx tests
use — render into an offscreen target, download with `bk__gfx_download_texture`, assert
known pixels within a tolerance, never a whole-buffer hash (backends do not rasterize
bit-identically).

The properties worth proving, each chosen because it fails differently:

- **Ordering.** Two overlapping opaque quads drawn in one frame; the second must win at
  the overlap. Then the same frame with the draw order swapped — the *other* one wins.
  A single-order test passes against an implementation that replays backwards.
- **State is sticky, and per-record.** Bind pipeline + texture A, draw; bind texture B,
  draw. Both draws must sample their own texture — proving the record captured state at
  call time rather than reading the final frame state at replay time. This is the single
  most likely implementation bug and the reason records snapshot rather than reference.
- **Instancing.** `bk_gfx_draw_instanced(4, N)` with per-instance offsets from a vertex
  storage buffer puts N quads at N distinct locations. Assert a pixel inside each, and
  one between them that must stay background — otherwise a shader ignoring
  `gl_InstanceIndex` (drawing N overlapping quads at one spot) would pass.
- **Uniforms reach the shader.** Push an MVP that translates by a known amount; assert
  the quad moved. Push a different one the next frame; assert it moved again — proving
  the push is per-frame, not latched once.
- **Uniform data is copied, not referenced.** Push from a stack buffer inside a helper
  function that returns before flush, then overwrite that stack region. The draw must
  still use the pushed values. This is the lifetime bug the arena copy exists to prevent
  and it is invisible without a deliberate test.
- **Scissor.** Draw a full-target quad with a scissor covering the left half; assert
  left is quad-colored and right is clear. Then reset with a zero rect and confirm the
  full target draws.
- **Blend modes.** Additive over a known background saturates upward, multiply darkens,
  screen lightens — one assertion each against hardcoded expected values, not values
  derived from the same formula the implementation uses.
- **Empty frame.** A frame with no draws at all still clears and presents (the
  regression `test_gfx.c` already covers, re-checked under the list model).
- **Per-record depth-format mismatch is caught.** Two records whose pipelines declare
  different depth-stencil formats must trip the named `BK_ASSERT` at the second record's
  bind, not fall through to an SDL_GPU validation failure. Without this the assert PR #18
  added silently covers only the frame's first pipeline.

Existing tests are the other half of the suite: `test_gfx.c`, `test_gfx_pipeline.c`,
`test_gfx_texture.c`, `test_gfx_canvas.c`, `test_gfx_capture.c` and `test_gfx_resize.c`
must pass **unmodified**, which is what proves §3's compatibility claim. Two tests do
change, but only for the §6 enum rename — `test_gfx_buffer.c` and `test_gfx_compute.c`
each swap one identifier, with no change to what they assert. If any *other* existing
test needs touching, the draw list broke compatibility and §3's claim is wrong.

## 8. Sample — `07_instanced`

N quads whose per-instance position and color come from a storage buffer, drawn in one
`bk_gfx_draw_instanced` call, with the camera as a pushed vertex uniform built from
`bk_m3x2_ortho` — the first user of P3.1's math.

`shaders/instanced.vert`/`.frag` are **deliberately throwaway**: a minimal proof of the
substrate, sitting beside `triangle`/`textured`/`gradient` as samples 03–05 already do.
They are not a first draft of CF's `inst_vs`, whose shape depends on the command format
P3.3 defines. Accepts `--frames N` like the other samples.

**MSL binding order is the live risk here** (P2 sub-project 2's spec §3, and the reason
that spec made it a gate). SDL_GPU's MSL convention packs `[[buffer]]` as uniform buffers
first, then storage buffers, with vertex buffers starting at `[[buffer(14)]]`. Adding
vertex-stage storage buffers *and* a uniform block to the same shader is exactly where
`spirv-cross`'s own indexing scheme can disagree. After generating each `.msl`, run:

```bash
grep -E '\[\[(texture|sampler|buffer)\(' shaders/instanced.vertex.msl shaders/instanced.fragment.msl
```

and check the emitted indices against that ordering. Fix mismatches with the
`--msl-*-binding` remap flags chosen against the real output, add any flags used to
`cmake/shaders.cmake`, and record them in `DEVIATIONS.md` alongside the existing
glslc/spirv-cross entry.

## 9. Decisions and rationale (do not relitigate in implementation sessions)

- **Draw list, not a draw-list *recording API*.** `PLAN.md` §8 reserves general
  draw-list recording for P3; this is that phase. But the list is an internal replay
  buffer with the same public call shape as today, not a new user-facing record/replay
  API — that distinction is what keeps four samples working unedited.
- **Records snapshot state; they do not reference it.** The alternative (records holding
  a pointer to shared state) makes every draw in a frame see the *last* frame state,
  which is the ordering bug §7 tests for explicitly.
- **The depth-format assert moves from once-per-frame to once-per-record.** PR #18 added
  `BK_ASSERT(bk__gfx_pipeline_depth_format(pipeline) == depth_target_format)` inside the
  render pass to surface a depth mismatch as a named assertion instead of SDL_GPU's
  opaque "pipeline incompatible with render pass" validation failure. With one pipeline
  per frame that was one check; with a list it must run at each record's pipeline bind,
  or a frame mixing a depth-enabled and a depth-disabled pipeline loses the diagnostic
  for whichever one isn't first.
- **Records are arena-allocated and chained**, not a growable array: the frame arena
  cannot realloc in place, and chaining avoids copying when the frame's draw count is
  unknown.
- **Uniform bytes are copied into the arena.** `SDL_PushGPU*UniformData` runs at flush,
  long after the caller's stack frame is gone.
- **Canvas stays frame-level** (§5), with the upgrade path written down.
- **Storage buffer usage is one enum value for both graphics stages**, because SDL uses
  one creation flag; the vertex/fragment distinction lives at bind time only.
- **`BK_GFX_BUFFER_USAGE_STORAGE_READ` is renamed to `_STORAGE_COMPUTE`.** Keeping the
  old name beside a new `_STORAGE_GRAPHICS` would be asymmetric and invite exactly the
  wrong-usage mistake that fails silently at draw time. Two call sites; mechanical.
- **Scissor and viewport take `BK_Rect`, not `SDL_Rect`.** `PLAN.md` prefers raw SDL
  types where wrapping adds nothing, but `BK_Rect` was added in P3.1 for this exact
  consumer, and the draw API above it is `bk_`-typed throughout.
- **A `<= 0` width or height means "unset"**, rather than a separate
  `bk_gfx_clear_scissor()`. One function, one concept, and it matches CF's own
  `{0,0,-1,-1}` sentinel.
- **Uniform slot 0 only**, both stages. CF's `inst_vs` uses vertex set 1 binding 0 and
  `tile_fs` uses fragment set 3 binding 0; nothing in Phase 3 needs a second slot.
- **No indexed-instanced draw**, no per-draw canvas, no compute-in-frame: each is
  deferred to the sub-project that first needs it.

## 10. Explicitly out of scope

- **Compute dispatch inside the frame command buffer**, read-write storage buffers, and
  per-frame cycled/streaming uploads — P3.6 (tiled path).
- **`bk_draw.h`, the command format, sprite batching, SDF shaders** — P3.3 onward.
- **Multiple vertex buffer slots, multiple bound textures** — one slot each still; P3.3's
  batch design decides the real multi-resource shape.
- **Per-draw canvas targeting** — §5.
- **Indexed instanced draws, uniform slots beyond 0, indirect draws.**
- **A public `BK_GfxDrawCmd` type** — the record is internal
  (`src/internal/bk_gfx_internal.h`). P3.3's `BK_DrawCmd` is a different, GPU-uploaded
  thing; do not conflate them.
- **Full SDL_GPU blend-factor/op matrix** — five named modes, as CF has.
