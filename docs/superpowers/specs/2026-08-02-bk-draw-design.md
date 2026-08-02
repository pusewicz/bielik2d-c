# Bielik2D — Phase 3, Sub-project 3: `bk_draw`

## 0. Context and scope

Phase 3 ports Cute Framework's command renderer (`src/cute_draw.cpp`,
`tools/builtin_shaders.h` in the CF checkout). P3.1 landed the math it needs, P3.2 landed
the gfx substrate it needs. This sub-project is the first one that draws something: the
public `bk_draw.h` API, the record→collate→replay pipeline behind it, and a
transliteration of CF's **instanced** SDF path.

Phase 3 ships as six sub-projects. Only P3.1 and P3.2 had been written down; the rest are
recorded here so the sequence is a plan rather than an accumulation of forward references:

| | | Status |
|---|---|---|
| P3.1 | `bk_math` — 2D math, `BK_Rect`, `bk_m3x2_ortho` | landed, PR #20 |
| P3.2 | gfx substrate — draw list, instanced draws, graphics storage buffers, uniforms, scissor/viewport | landed, PR #23 |
| **P3.3** | **`bk_draw.h` — unified SDF instanced renderer, Space-Delivery shape set** | **this spec** |
| P3.4 | runtime atlas cache + `BK_Sprite`, `bk_draw_sprite` | |
| P3.5 | wide shapes — polyline, polygon, bezier paths, CSG groups, custom SDFs | |
| P3.6 | tiled compute path — GPU binning, opaque-cover cull | |

This decomposition is **not** a `PLAN.md` deviation. §7's P3 ("record/flush draw list,
sprite batch, atlas, SDF shapes") lands in full across P3.1–P3.6, and Phase 2 already
established that a phase ships as sub-projects each carrying their own spec.

**The single design fact that shapes everything below:** CF has no separate sprite
renderer. `BatchGeometry` (`src/internal/cute_draw_internal.h:64`) covers `SPRITE`,
`QUAD`, `CIRCLE`, `CAPSULE`, `SEGMENT`, `TRI`, `POLYGON` and the rest in one stream,
expanded by one instanced vertex shader and evaluated as SDFs in one fragment shader.
Sprites and shapes batch together and preserve paint order because they are the same
thing. P3.3 ports that unification rather than building a textured-quad batcher that a
later sub-project would throw away.

### 0.1 Scope bounded by a real consumer

The shape set and the state-stack surface are both bounded by what Space Delivery — the
v1 finish line — actually calls, measured by grep over its `src/`:

| CF call | uses | | CF call | uses |
|---|---|---|---|---|
| `push`/`pop_color` | 28 | | `push`/`pop_layer` | 6 / 5 |
| `draw_push`/`pop` (camera) | 6 | | `push`/`pop_shape_aa` | 6 |
| `push`/`pop_scissor` | 1 | | `push_blend`, `push_filter`, `push_shader`, `push_render_state` | **0** |

Shapes used: `box_fill`, `box`, `box_rounded[_fill]`, `circle_fill`, `circle2`, `line`,
`tri_fill`, `quad_fill2`, `arrow`, `sprite`, `sprite_9_slice`, `canvas`, plus `text`
(P5, out of scope here).

Two consequences, both taken as decisions:

- **The blend/filter/shader/render-state stacks CF carries are not ported.** Space
  Delivery's one custom-shader effect goes through `cf_make_material`/`cf_apply_shader` —
  the raw graphics API — which in Bielik2D means dropping to `bk_gfx_*` directly, exactly
  the escape hatch P3.2 §3 preserved. P3.3 therefore ships **one** blend mode
  (premultiplied alpha) and no blend API. §5 shows how that simplifies batching.
- **Layers are in scope.** Six uses is a real consumer, and layers are the one feature
  here that changes the shape of collate (§5).

## 1. File layout

```
bielik2d/
  cmake/
    embed_shader.cmake        (new; turns committed bytecode into a C header, §5.0)
  include/bielik/
    bk_draw.h                  (new; §2)
    bk_gfx_pipeline.h          (edit: BK_GFX_VERTEX_FORMAT_FLOAT, appended to the enum)
  src/
    bk_draw.c                  (new; record + collate + pipeline/shader ownership)
    bk_gfx_pipeline.c          (edit: the new vertex format's attribute-size case)
    internal/
      bk_draw_internal.h       (new; BK_DrawGeom, BK_DrawCmd,
                                bk__draw_init / bk__draw_collate / bk__draw_shutdown)
      bk_gfx_internal.h        (edit: add bk__gfx_pending_target_depth_format, §5.0)
      bk_gfx_texture_internal.h (edit: add bk__gfx_texture_size, §5.0)
    bk_gfx.c                   (edit: implement bk__gfx_pending_target_depth_format)
    bk_gfx_texture.c           (edit: implement bk__gfx_texture_size)
    bk_app.c                   (edit: three calls -- init, collate, shutdown, §5)
  DEVIATIONS.md                (edit: issue #21 reassigned away from P3.3, §7)
  NOTICE.md                    (edit: zlib attribution for the CF port, §7)
  shaders/
    draw.vert / draw.frag      (new; §6)
  samples/08_draw/             (new; §8)
  tests/
    test_draw.c                (new; CPU, CI-required)
    test_draw_gpu.c            (new; GPU, CI allow-failure)
    test_header_bk_draw.c      (new; standalone-compile check)
```

`bk_gfx` needs one new internal accessor, `bk__gfx_texture_size(const BK_GfxTexture *,
i32 *out_w, i32 *out_h)`. `bk_draw` needs it twice: to normalize `bk_draw_texture`'s
`src_px` texel rect into UVs, and to size the default projection when a canvas is bound
(via `bk_gfx_canvas_texture`). No public API is added — `bk_gfx_texture_internal.h`
already carries the handle/format accessors in exactly this shape.

## 2. Public API — `include/bielik/bk_draw.h`

```c
#pragma once
#include <bielik/bk_math.h> // BK_Aabb, BK_Color, BK_M3x2, BK_Rect, BK_V2
#include <bielik/bk_types.h>

typedef struct BK_GfxTexture BK_GfxTexture;

/// Depth of every bk_draw state stack, including the camera's.
constexpr i32 BK_DRAW_STACK_MAX = 64;

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

/// Overrides this frame's projection. The default, applied every frame unless this is
/// called, is bk_m3x2_ortho over the render target's size -- origin at the center, y up,
/// one unit per pixel, matching bk_m3x2_ortho's documented contract. The override is
/// consumed by the frame's collate, so call it every frame you want it; there is no
/// resize event to handle, because the default is recomputed from the current target
/// each frame.
void bk_draw_set_projection(BK_M3x2 projection);

/// Saves the current camera transform. Depth is capped (BK_DRAW_STACK_MAX, 64);
/// overflowing BK_ASSERTs in Debug and is ignored in Release.
void bk_draw_push(void);

/// Restores the camera transform saved by the matching bk_draw_push. Popping the base
/// entry BK_ASSERTs in Debug and leaves the base in place in Release.
void bk_draw_pop(void);

/// The current camera transform -- world space to eye space, projection not applied.
[[nodiscard]] BK_M3x2 bk_draw_peek(void);

/// Translates the camera transform. Composes onto the current top, like the rest of the
/// transform calls: the last call applied is the innermost.
void bk_draw_translate(BK_V2 offset);

/// Rotates the camera transform by radians, counter-clockwise.
void bk_draw_rotate(f32 radians);

/// Scales the camera transform. A zero component makes the transform singular, which is
/// legal -- subsequent draws are culled at record time rather than rasterized (§5).
void bk_draw_scale(BK_V2 scale);

/// Composes transform onto the current camera transform.
void bk_draw_transform(BK_M3x2 transform);

// ---------------------------------------------------------------------------
// State stacks
//
// Each is a plain push/pop/peek triplet over a 64-deep stack reset to its default at
// the end of every frame's collate, so a push/pop imbalance cannot leak into the next
// frame. pop returns the value it removed.
// ---------------------------------------------------------------------------

/// Sets the color for subsequent shapes, and the tint for subsequent textures. Straight
/// (non-premultiplied) alpha -- bk_color_premultiply runs at record time, because the
/// one blend mode is premultiplied. Default: bk_color_white().
void bk_draw_push_color(BK_Color color);
BK_Color bk_draw_pop_color(void);
[[nodiscard]] BK_Color bk_draw_peek_color(void);

/// Sets the layer for subsequent draws. Higher layers paint later. Within one layer,
/// record order decides. Default: 0.
void bk_draw_push_layer(i32 layer);
i32 bk_draw_pop_layer(void);
[[nodiscard]] i32 bk_draw_peek_layer(void);

/// Sets the antialiasing band width in pixels for subsequent shapes. 0 disables it --
/// the pixel-art path. Ignored by bk_draw_texture, whose edges are the sampler's
/// business. Default: 1.0f.
void bk_draw_push_antialias(f32 aa_px);
f32 bk_draw_pop_antialias(void);
[[nodiscard]] f32 bk_draw_peek_antialias(void);

/// Clips subsequent draws to rect, in pixels of the render target with a top-left
/// origin -- the same meaning bk_gfx_set_scissor documents, and the same "width or
/// height <= 0 means no scissor" convention. Default: a zero rect.
void bk_draw_push_scissor(BK_Rect rect);
BK_Rect bk_draw_pop_scissor(void);
[[nodiscard]] BK_Rect bk_draw_peek_scissor(void);

// ---------------------------------------------------------------------------
// Shapes
//
// All coordinates are world space, transformed by the camera transform captured at
// record time. thickness strokes centered on the shape's boundary. radius rounds
// corners; 0 keeps them sharp. A thickness or radius large enough to swallow the shape
// is clamped by the SDF itself, not by the API.
// ---------------------------------------------------------------------------

void bk_draw_box_fill(BK_Aabb bb, f32 radius);
void bk_draw_box(BK_Aabb bb, f32 thickness, f32 radius);

void bk_draw_circle_fill(BK_V2 center, f32 radius);
void bk_draw_circle(BK_V2 center, f32 radius, f32 thickness);

/// A stroked segment with round caps -- a capsule SDF.
void bk_draw_line(BK_V2 p0, BK_V2 p1, f32 thickness);

void bk_draw_tri_fill(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 radius);
void bk_draw_tri(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 thickness, f32 radius);

/// A directed arrow from a to b: a shaft of the given thickness unioned with a head of
/// the given width, evaluated as one SDF so the seam never double-blends.
void bk_draw_arrow(BK_V2 a, BK_V2 b, f32 thickness, f32 head_width);

/// Draws texture's src_px sub-rect (in texels, top-left origin) onto the dst box in
/// world space, tinted by the current color and transformed by the camera. Passing a
/// src_px covering the whole texture draws it whole; passing sub-rects is how 9-slice
/// composes, and how P3.4's atlas will feed this same path. Each texture change starts a
/// new batch, so this costs one draw per distinct texture until the atlas lands.
///
/// texture's pixels must be **premultiplied**: RGB already scaled by A. The one blend
/// mode is premultiplied and the shader samples the texel as-is, so straight-alpha data
/// (an ordinary decoded PNG) composites over-bright -- a 50%-alpha white texel reads back
/// fully white instead of half-blended, and every soft edge and fade is wrong the same
/// way. Premultiply at load time.
void bk_draw_texture(BK_GfxTexture *texture, BK_Aabb src_px, BK_Aabb dst);
```

### 2.1 Three naming decisions

- **`bk_draw_texture`, not `bk_draw_sprite`.** P3.4 introduces `BK_Sprite` (atlas handle,
  pivot, animation) and wants `bk_draw_sprite(const BK_Sprite *)`. Naming the raw form
  `bk_draw_texture` leaves that free, so P3.3 does not design the sprite entry point
  twice or rename a shipped symbol. CF's `cf_draw_canvas` ports to
  `bk_draw_texture(bk_gfx_canvas_texture(canvas), …)` — `bk_gfx_canvas_texture` already
  returns a plain `BK_GfxTexture *`, so no new API is needed for it.
- **No `BK_Circle` type.** P3.1 shipped a deliberately minimal type set and declined to
  wrap shapes that carry no operations. Circle takes center and radius, matching CF's
  `cf_draw_circle2` rather than its `CF_Circle` overload.
- **`push_antialias(f32)`, not a `bool`.** Matches CF's `shape_aa` semantics — a band
  width in pixels, where 0 is off. Space Delivery's six uses are the pixel-art off
  switch, which a float serves as well as a bool while keeping the band tunable.

## 3. Decision: one blend mode, so batches split on two things

CF's `BatchGeometry` carries a `blend` field and splits runs at blend changes
(`cute_draw_internal.h:83`). P3.3 has no blend API at all, so a batch splits on exactly
two state changes:

- **texture** — a different `BK_GfxTexture *`, including texture-to-none;
- **scissor** — a different `BK_Rect`.

Everything else (color, layer, antialias, camera transform, shape type) travels *inside*
the per-command data and needs no split.

This is what makes the layer sort safe. Sorting reorders draws, and reordering across a
blend-mode change would silently change the composite. With one blend mode there is no
such change to cross. When P3.5 or a game needs a second blend mode, the sort must become
segment-local or blend must join the sort key — recorded here so that arrives as a
decision rather than a bug.

## 4. Command format

CF's GPU command is five `vec4`s (`CF_TileCmd`, `cute_draw_internal.h:148`). P3.3 packs
**three**, dropping every field whose only consumer is a sub-project that does not exist
yet:

```c
// Mirrors `Cmd` in draw.vert (std430).
typedef struct BK_DrawCmd {
  u32 meta[4];       // type, color_rg, payload offset (vec4 units), mvp offset
  f32 shape[4];      // radius, half-stroke, aa (world units), unused
  f32 misc[4];       // fill, color_ba (as float bits), 2 unused
} BK_DrawCmd;
static_assert(sizeof(BK_DrawCmd) == 48, "std430 layout: three vec4s");
```

Dropped relative to CF: `user[4]` (custom-shader params, P3.5 only), `n` (polygon vertex
count, P3.5 only), `opaque` (opaque-cover cull, P3.6 only), and `aabb[4]`.

**`aabb` goes too.** CF's instanced vertex shader derives each shape's coverage quad from
its payload parameters and never reads `aabb`; the field exists purely for the tiled
path's binning, and P3.3 does no CPU-side culling that would want bounds either. Three
`vec4`s is 48 bytes and still std430-aligned, so keeping it would buy no layout stability
— it would just be the speculative option `CLAUDE.md` forbids, pointed at P3.6 instead of
P3.5. Widening the struct when P3.5 and P3.6 arrive is a contained change to the packer
and the shader's `Cmd` declaration, with **no public API impact**.

**Colors travel as packed half4**, two `packHalf2x16` words (`meta[1]` = rg,
`misc[1]` = ba), following CF. Not `unorm8`: it keeps HDR channels above 1.0 intact, and
premultiplied colors can legitimately exceed the unorm range.

**Payload** is a flat `vec4` array indexed in `vec4` units by `meta[2]`. Per shape type it
holds the geometry parameters (endpoints, centers, half-extents, UV bounds); per command
it also references a **matrix palette** entry at `meta[3]`.

**The MVP is per command, not a frame uniform.** The camera `push`/`pop` stack means the
transform genuinely varies between draws within one frame, so it cannot be a uniform
pushed once per batch — it rides in the palette, and `meta[3]` selects the entry. Collate
emits a new palette entry only when a record's transform differs from the previous
record's, so a frame that never touches the camera costs one entry total. This is
independent of the **batch-base** uniform §5 step 4 and §6 describe: that one carries a
batch's starting index into the shared `cmds` array, not the MVP.

**The matrix is the forward MVP only — 2 `vec4`s, not 4.** CF's instanced vertex shader
emits interpolated world-space position as a varying (`v_pos_uv.xy`), and `s_draw_fs`
recovers world space from that varying. It never reads an inverse. The inverse MVP in
CF's `CF_TileCmd` exists solely for the tiled path, whose fragment shader has no
rasterized per-shape quad to interpolate from. See §7 for what this means for issue #21.

## 5. Data flow: init → record → collate → replay

### 5.0 Init and shutdown

`bk__draw_init()` runs from `bk_app.c` once the GPU device exists, alongside the existing
`bk__gfx_configure_swapchain_depth` call; `bk__draw_shutdown()` runs beside
`bk__gfx_shutdown()`, before `SDL_DestroyGPUDevice`. It owns three things:

- the compiled `draw.vert`/`draw.frag` shader objects, whose bytecode arrives as a
  CMake-generated header: `cmake/embed_shader.cmake` turns each shader's four committed
  bytecode files (`.vertex.spv`/`.msl`, `.fragment.spv`/`.msl`) into byte arrays in
  `${CMAKE_BINARY_DIR}/generated/bk_draw_shaders.h`, which `src/bk_draw.c` includes;
- the static **corner buffer** — six floats, `{0,1,2, 0,2,3}`, uploaded once, bound as
  vertex buffer slot 0 for every batch;
- the **pipelines**, of which there are at most **two**.

Two, not one, because `bk_gfx.h:80-83` `BK_ASSERT`s that a bound pipeline's
`depth_stencil_format` equals the render pass's attachment format. A game may bind a
depth-enabled canvas (`BK_GfxCanvasDesc.depth_stencil`) or enable swapchain depth
(`BK_AppDesc.window.depth_stencil`) and then call `bk_draw_*`, and a single no-depth
pipeline would assert on exactly that case — a case the framework already supports and
that neither the sample nor the tests would otherwise reach. The count is bounded at two
because `bk_gfx_depth_stencil_format` probes and returns exactly **one** format per
device, so the reachable set is `{INVALID, that format}`. Both are created eagerly at
init; collate selects per frame from `bk__gfx_pending_target_depth_format()`, not from
`bk__gfx_canvas_depth_format(bk__gfx_get_pending_canvas())` directly — that call
`BK_ASSERT`s a non-null canvas, which trips on the common no-canvas-bound case, and it
also can't see `BK_AppDesc.window.depth_stencil`. The new accessor
(`src/internal/bk_gfx_internal.h`) covers both cases: a bound canvas's own depth format,
else the framework-owned swapchain depth format if enabled, else `INVALID` (see
`DEVIATIONS.md`). `bk_draw` writes no depth and does no depth testing in either variant —
the second pipeline exists solely to satisfy the format match.

### Record

Each `bk_draw_*` call reads the current stack tops, composes the camera transform, and
links one `BK_DrawGeom` into an arena-allocated singly-linked chain — the same allocator
and the same chain rationale P3.2 §3 gives for `BK_GfxDrawCmd` (`bk_frame_alloc` cannot
realloc in place; a chain avoids copying when the frame's draw count is unknown up front).

`BK_DrawGeom` holds the shape type, the premultiplied color, the shape parameters, the
layer, the scissor, the antialias width, the texture pointer, and the composed transform.

**Singular-transform cull.** If the camera transform's determinant
(`x.x*y.y − y.x*x.y`) is zero, the record is dropped and no `BK_DrawGeom` is linked. No
log and no assert: a sprite scaled to zero is legitimate game state that correctly draws
nothing. This is a cull, not an error path — it skips packing and uploading a record whose
quad would rasterize to zero area anyway. It checks the camera transform, not the
composed MVP, because the projection is not resolved until collate; `bk_m3x2_ortho` is
non-singular for any non-zero target size, so the two agree unless a caller supplies a
singular projection of its own, which collapses the whole frame and is the caller's
error.

### Collate

`bk__draw_collate()` is internal, declared in `src/internal/bk_draw_internal.h`, and
called from `bk_app.c` between `s_app.desc.render(...)` and `bk__gfx_flush()`. There is no
public flush: per-draw canvas targeting is deferred by P3.2 §5, so there is nothing a
mid-frame flush would express.

1. **Snapshot the chain head/tail and clear them first**, before anything that can fail or
   return early. Non-negotiable, for exactly the reason P3.2 §3 documents: `bk_app.c` runs
   `bk__gfx_flush()` then `bk__arena_reset()` unconditionally, and a minimized or occluded
   window takes an early return out of flush. State left pointing into arena memory across
   a reset is a use-after-free the moment the arena has grown via `bk__realloc`.
2. **Stable-sort by `(layer, record_id)`.** Equal layers therefore keep record order, so
   layers are a coarse ordering key layered *over* paint order rather than replacing it.
   This sort lives **inside `bk_draw`**: it runs before any `bk_gfx_*` call, so P3.2 §3's
   "flush replays every recorded draw in call order" contract is untouched and `bk_gfx`
   never learns that layers exist.
3. **Two-pass pack.** Walk the chain once to count records and payload `vec4`s; make one
   exact-sized `bk_frame_alloc` for each; walk again to fill them. Sizing exactly is what
   lets an allocator that cannot realloc produce the contiguous arrays the GPU needs. Peak
   arena use is roughly twice the record data for the duration of collate, since the
   packed copy coexists with the chain.
4. **Upload and emit.** Both arrays become `BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS` buffers.
   These cannot be destroyed before this call returns: `bk_gfx_bind_vertex_storage_buffer`
   only records the buffer pointer into this frame's draw chain, and `bk__gfx_flush`
   replays it *after* collate returns (`bk_app.c`'s call order), so an immediate destroy
   would free the buffer before flush dereferences it. `bk_draw` instead holds each
   frame's pair in file statics and destroys the *previous* frame's pair at the top of the
   *next* `bk__draw_collate` call — still one allocation pair per frame, just with a
   one-frame lifetime shift (see `DEVIATIONS.md`). Per batch: `bk_gfx_bind_pipeline` (the
   depth variant chosen per §5.0), `bk_gfx_bind_vertex_buffer` (the corner buffer),
   `bk_gfx_bind_vertex_storage_buffer` (cmds at slot 0, payload at slot 1),
   `bk_gfx_bind_texture` when the batch has one, `bk_gfx_set_scissor`,
   `bk_gfx_push_vertex_uniform` (the batch-base uniform, §4/§6), then
   `bk_gfx_draw_instanced(6, batch_count)`.
5. **Reset the state stacks** to their documented defaults.

The projection is resolved here, not at record time: each record stores its **camera**
transform, and pass two composes `projection × camera` when building that record's matrix
palette entry. That is why `bk_draw_set_projection` may be called at any point in the
frame and still applies to every draw in it.

**Where the default projection's size comes from.** With a canvas bound,
`bk__gfx_get_pending_canvas` → `bk_gfx_canvas_texture` → `bk__gfx_texture_size` (§1).
Otherwise the existing public `bk_window_size()` — **not** the swapchain texture's
dimensions, which are not available here: `bk_gfx.c:274` only learns them from
`SDL_WaitAndAcquireGPUSwapchainTexture` inside `bk__gfx_flush`, which runs *after* collate.
`bk_window_size()` is cached and refreshed on resize before any app handler runs, so this
needs no `SDL_GetWindowSizeInPixels` call of its own. The cached size and the swapchain's
can disagree for a single frame mid-resize, which is harmless: the projection is off by
the resize delta for one frame and self-corrects the next. Sizing from the window is also
what removes any need for a resize event.

### Replay

`bk__gfx_flush()` then replays the draw list `bk_draw` just appended to, exactly as it
would replay a game's hand-written `bk_gfx_*` calls. `bk_draw` records **through** the
public `bk_gfx` API rather than around it, so a game can interleave its own raw draws with
`bk_draw` calls in the same frame.

## 6. Shaders

`shaders/draw.vert` and `shaders/draw.frag`, GLSL compiled by the existing `glslc` +
`spirv-cross` path (`cmake/shaders.cmake`), transliterated from CF's `s_inst_vs` and
`s_draw_fs` instanced path.

**Vertex.** One `layout(location = 0) in float in_corner` attribute over a static 6-float
vertex buffer (two triangles' worth of corner indices), created once at init. Reads
`cmds[gl_InstanceIndex + u_batch_base.x]` and the payload, derives the coverage quad from
the shape parameters — CF's OBB fitting lives here, not on the CPU — and emits the SDF
varyings plus interpolated world-space position. Inflates coverage by `radius + stroke*2 +
aa` so the antialias band is never clipped. `u_batch_base` is a `set = 1, binding = 0`
uniform block (a single `uint`, padded to a `uvec4` for std140's block-size rule) carrying
the batch's first command index — needed because collate packs every batch's commands
into one shared `cmds` array back to back, but `SDL_DrawGPUPrimitives`' `first_instance` is
hardcoded to 0 (`bk_gfx_draw_instanced`, `src/bk_gfx.c`) and `gl_InstanceIndex` is not a
portable substitute: spirv-cross translates it to Metal's zero-based `[[instance_id]]`,
which excludes `baseInstance` entirely, unlike Vulkan's convention. See `DEVIATIONS.md`.

**Fragment.** Evaluates the shape's SDF at the interpolated world-space position, applies
the antialias band, samples `u_image` for texture records, and outputs premultiplied
color.

**One P3.2 entry point stays unused, deliberately.** For P3.3's shape set every fragment
parameter fits in the varyings — CF's fragment-side payload reads serve polygon vertex
lists and CSG operand lists, both P3.5 — so `bk_gfx_bind_fragment_storage_buffer` gets its
first consumer in P3.5. `bk_gfx_push_vertex_uniform` is no longer in this state: the
batch-base uniform above is its first consumer, ahead of the batch-uniform data §4
originally expected to be its first user.

**Resource counts.** `BK_GfxShaderDesc.num_samplers`, `num_storage_buffers`, and
`num_uniform_buffers` must match what each shader binary declares — `CLAUDE.md` records
that a mismatch fails silently at draw time, dropping the whole command buffer so the
target reads back all-zero with nothing logged. For P3.3: the **vertex** shader declares
2 storage buffers, 0 samplers, **1 uniform buffer** (the batch-base uniform above);
the **fragment** shader declares 1 sampler, 0 storage buffers, 0 uniform buffers. CF's
fragment uniform block (`u_texture_size`, `u_alpha_discard`, `u_use_smooth_uv`) serves
filter modes and alpha discard, both out of scope, so it is not ported.

## 7. Decisions and rationale (do not relitigate in implementation sessions)

- **Unified SDF renderer, not a sprite batcher first.** Splitting them means writing a
  sprite shader that P3.5 throws away, and leaves sprites and shapes in two batches that
  cannot interleave by paint order without extra machinery.
- **One blend mode.** §3. Zero uses in the target game; the raw `bk_gfx` path covers the
  custom-effect case; and it is what makes the layer sort safe without segment-local
  sorting.
- **Layer sort lives in `bk_draw`, not `bk_gfx`.** §5. Keeps P3.2 §3's replay contract
  intact.
- **Chain then two-pass pack, not a second allocator.** §5. Rejected: a growable
  non-arena buffer (records land contiguously and collate needs no packing copy, but it
  introduces an allocator alongside the frame arena and memory that never shrinks after a
  heavy frame); and recording straight into a mapped transfer buffer (fewest copies, but
  writing in record order forecloses the layer sort entirely).
- **Four-`vec4` command, not five.** §4. `user` and `n` have no consumer before P3.5,
  `opaque` none before P3.6, and widening later touches only the packer and the shader
  struct.
- **Issue #21 moves to P3.6.** P3.2 §5 assigned "the draw layer must not build an inverse
  MVP from a singular transform" to P3.3. Reading CF's instanced path end to end shows it
  never builds an inverse — world space arrives as an interpolated varying (§4). The
  hazard first arises in P3.6's tiled path, whose fragment shader does invert the MVP.
  [bielik2d-c#21](https://github.com/pusewicz/bielik2d-c/issues/21) is retargeted to P3.6
  rather than closed. P3.3's record-time cull (§5) forecloses it early, but is justified
  on its own terms as a cull and is not the fix #21 asks for. Because this diverges from
  an obligation a prior spec explicitly assigned to P3.3, it gets a `DEVIATIONS.md` entry
  as well — `CLAUDE.md` scopes that file to divergence from `PLAN.md` *or a task brief*,
  and P3.2 §5 is the brief here.
- **Two pipelines, not one.** §5.0. Rejected: restricting `bk_draw` to non-depth targets,
  which would make a supported `bk_gfx` configuration silently incompatible with the draw
  layer.
- **Attribution.** `PLAN.md` §7 requires zlib attribution in `NOTICE.md` for the CF port.
  P3.3 is the sub-project that first transliterates CF shader and renderer code, so it
  adds that entry.

## 8. Testing and sample

The CPU/GPU split PR #23 established: CPU-only tests are CI-required on every platform,
GPU tests join the allow-failure group (`.github/workflows/ci.yml`; no DXIL variant exists
for Windows — see `DEVIATIONS.md`).

**`tests/test_draw.c`** — no GPU, CI-required:

- batch splitting on texture change, on scissor change, and *not* on color, layer,
  antialias, or shape-type change;
- the stable sort — layers order correctly, and equal layers preserve record order;
- transform composition through `push`/`pop`, and `peek` after each;
- the singular-transform cull drops the record;
- the two-pass packer's record count, payload `vec4` count, and per-command payload
  offsets;
- the matrix palette — a frame that never touches the camera emits exactly one entry, a
  `push`/`transform`/`pop` sequence emits the entries its distinct transforms require, and
  every command's `meta[3]` selects the transform it was recorded under;
- `BK_DrawCmd` is 48 bytes with the field offsets the shader's `Cmd` expects.

**Stack overflow and underflow are deliberately not tested.** Both are `BK_ASSERT`s, which
wrap `SDL_assert` and are live in Debug — and CI runs `test_draw` in *both* Debug
(`ci.yml:124`) and Release (`ci.yml:70`), so a test that exercised either would abort the
Debug leg. Asserting is still the right behaviour: a `push`/`pop` imbalance is a caller
bug, and `CLAUDE.md` forbids silent failure. The contract is stated in the header (§2) and
left unexercised rather than softened into something testable.

**`tests/test_draw_gpu.c`** — real device, allow-failure group. Probes specific pixels the
way `test_gfx_drawlist_gpu.c` does rather than hashing whole frames: SDF antialiasing will
not hash-match across Metal and Vulkan, so `CLAUDE.md`'s "render to texture, hash, compare"
is not achievable for this module and the established probe-pixel convention applies. Per
shape: an interior pixel carries the fill color, a pixel well outside is clear, and for a
stroked variant the center is clear. Plus one layer-ordering case where a low layer is
provably overdrawn by a high one recorded before it, and
`test_second_batch_reads_its_own_commands` — two boxes under different scissors, each in
its own screen half, guarding the batch-base uniform (§4/§6, `DEVIATIONS.md`) against a
second batch silently replaying the first batch's commands.

**`samples/08_draw`** — every shape in the set, a rotating camera `push`/`pop`, a layer
inversion, and antialias on/off side by side. Accepts `--frames N` like the other samples.

## 9. Explicitly out of scope

- **Atlas, `BK_Sprite`, `bk_draw_sprite`, 9-slice as a call** — P3.4. 9-slice composes
  from nine `bk_draw_texture` calls with sub-rect UVs until then.
- **Polyline, polygon, bezier, baked paths, CSG shape groups, custom user SDFs** — P3.5,
  along with the fragment-side payload reads and the `user`/`n` command fields they need.
- **Tiled compute path, GPU binning, opaque-cover cull, inverse MVP** — P3.6.
- **Text and fonts** — P5.
- **Blend modes, filter modes, per-draw shaders, render state, alpha discard,
  per-vertex colors and attributes** — no consumer; the raw `bk_gfx` path covers the
  custom-effect case.
- **Per-draw canvas targeting** — deferred by P3.2 §5; canvas stays frame-level.
- **A public flush** — nothing it would express while canvas is frame-level.
