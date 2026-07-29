# Bielik2D — Phase 2, Sub-project 1: Shader Toolchain + Graphics Pipelines

## 0. Context and scope

`PLAN.md` section 7 scopes Phase 2 ("P2 gfx core") as: pipelines, buffers, textures,
offline shader compile (SDL_shadercross toolchain), canvases/render targets. That is
too much for one module-per-session unit of work, so P2 is decomposed into three
sub-projects, each getting its own spec → plan → implementation cycle:

1. **This spec**: shader compile toolchain + graphics pipeline objects.
2. Buffers + textures + compute pipelines.
3. Canvases/render targets + resize handling + depth-stencil.

Compute pipelines were considered for sub-project 1 but deferred to sub-project 2:
a compute pipeline has no working example until storage buffers/textures exist to
dispatch against, and shipping it as inert create/destroy plumbing with no test isn't
worth the added surface area this session.

Phase 1 already provides: SDL_GPU device creation (`bk_gpu()`), window claim
(`bk_window()`), swapchain present with a clear-only render pass (`bk__gfx_flush` in
`src/bk_gfx.c`). This sub-project adds the first real content between "acquire
swapchain texture" and "submit command buffer": a bound pipeline drawing something.

## 1. Module boundaries and file layout

One new module, following the existing one-module-per-`bk_<name>` convention:

```
bielik2d/
  cmake/
    shadercross.cmake        (new)
  shaders/                   (new)
    triangle.vertex.hlsl
    triangle.fragment.hlsl
  include/bielik/
    bk_gfx_pipeline.h        (new)
  src/
    bk_gfx_pipeline.c        (new)
    internal/
      bk_gfx_pipeline_internal.h  (new, if the sample/test need internal accessors)
  samples/
    03_triangle/main.c       (new)
    03_triangle/CMakeLists.txt (new)
  tests/
    test_gfx_pipeline.c      (new)
```

`bk_gfx.h`/`bk_gfx.c` (device/window accessors, clear color) are untouched. There is
no separate shader module or public shader handle — `BK_GfxShaderDesc` is a plain
input struct declared in `bk_gfx_pipeline.h` and consumed by
`bk_gfx_pipeline_create()`. This mirrors SDL_GPU's own object model: a shader is
created, referenced by pipeline creation, and released immediately after — nothing in
this framework re-references a shader once its pipeline exists, so a persistent
`BK_GfxShader` handle would be ceremony with no reuse case.

## 2. Shader compile toolchain

`cmake/shadercross.cmake` defines a CMake function:

```cmake
bk_compile_shader(TARGET <target> SOURCE <path.hlsl> STAGE vertex|fragment)
```

It invokes the `shadercross` CLI (from SDL_shadercross, `FetchContent`'d alongside
SDL3) three times per source, once per output format, producing:

```
$<TARGET_FILE_DIR:target>/shaders/<name>.<stage>.spv
$<TARGET_FILE_DIR:target>/shaders/<name>.<stage>.dxil
$<TARGET_FILE_DIR:target>/shaders/<name>.<stage>.msl
```

i.e. right next to the built executable — no install step needed for samples/tests to
load them with a relative path at runtime.

This is a **build-time** dependency (the `shadercross` binary must be available to
CMake), not a runtime one. CI needs it on `PATH` or fetched/built alongside SDL3 —
exact CI wiring is an implementation detail for the plan, not this spec.

Runtime loading is caller-owned, not a `bk_gfx_pipeline` responsibility: samples and
tests read the three files for a given shader stage with plain `SDL_LoadFile` calls
and populate a `BK_GfxShaderDesc`'s three variant fields directly.
`bk_gfx_pipeline.h` has no opinion on filesystem paths or the VFS — consistent with
the project's asset-pack loading not existing until P6, and `#embed` being reserved
for later phases.

## 3. Public API — `bk_gfx_pipeline.h`

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <stddef.h>
#include <stdint.h>

/// One precompiled shader bytecode blob for a single backend format.
typedef struct BK_GfxShaderVariant {
    const void *code;
    size_t code_size;
    const char *entry_point;
} BK_GfxShaderVariant;

/// One shader stage, precompiled to all three backend formats; bk_gfx_pipeline_create
/// selects the variant matching the device's supported shader formats
/// (SDL_GetGPUShaderFormats). Resource counts must match what the shader binary
/// declares (SDL_GPU validates this at creation).
typedef struct BK_GfxShaderDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    int num_samplers;
    int num_uniform_buffers;
} BK_GfxShaderDesc;

/// Per-vertex attribute formats supported by pipeline vertex input state.
typedef enum BK_GfxVertexFormat {
    BK_GFX_VERTEX_FORMAT_FLOAT2,
    BK_GFX_VERTEX_FORMAT_FLOAT3,
    BK_GFX_VERTEX_FORMAT_FLOAT4,
    BK_GFX_VERTEX_FORMAT_UBYTE4_NORM, // packed color
} BK_GfxVertexFormat;

/// One vertex attribute: which shader input location it feeds, which vertex buffer
/// slot it reads from, its format, and its byte offset within that slot's stride.
typedef struct BK_GfxVertexAttribute {
    uint32_t location;
    uint32_t buffer_slot;
    BK_GfxVertexFormat format;
    uint32_t offset;
} BK_GfxVertexAttribute;

/// One vertex buffer slot's stride, in bytes.
typedef struct BK_GfxVertexBufferLayout {
    uint32_t slot;
    uint32_t pitch;
} BK_GfxVertexBufferLayout;

typedef enum BK_GfxPrimitiveType {
    BK_GFX_PRIMITIVE_TRIANGLE_LIST,
    BK_GFX_PRIMITIVE_TRIANGLE_STRIP,
    BK_GFX_PRIMITIVE_LINE_LIST,
} BK_GfxPrimitiveType;

/// Fixed-function blend state. Two modes cover 2D's needs; more get added when a
/// real use case needs SDL_GPU's full blend-factor/op matrix.
typedef enum BK_GfxBlendMode {
    BK_GFX_BLEND_NONE,
    BK_GFX_BLEND_ALPHA,
} BK_GfxBlendMode;

/// Opaque graphics pipeline: compiled shaders + fixed-function state, bound in a
/// render pass before a draw call. Owns no per-frame resources.
typedef struct BK_GfxPipeline BK_GfxPipeline;

typedef struct BK_GfxPipelineDesc {
    BK_GfxShaderDesc vertex_shader;
    BK_GfxShaderDesc fragment_shader;

    // nullptr/0 => no vertex input (e.g. a fullscreen/procedural triangle driven by
    // SV_VertexID with no bound vertex buffer).
    const BK_GfxVertexBufferLayout *vertex_buffers;
    int num_vertex_buffers;
    const BK_GfxVertexAttribute *vertex_attributes;
    int num_vertex_attributes;

    BK_GfxPrimitiveType primitive_type;

    // Caller supplies the target format explicitly — SDL_GetGPUSwapchainTextureFormat
    // today; a canvas's own format once sub-project 3 lands. No bk_ wrapper needed.
    SDL_GPUTextureFormat color_target_format;
    BK_GfxBlendMode blend_mode;
} BK_GfxPipelineDesc;

/// Creates a graphics pipeline. Logs via SDL_Log ("BK: " prefix) and returns nullptr
/// on any SDL_GPU failure (bad bytecode, unsupported format/resource combination) —
/// this is a runtime-data-dependent operation, not a programmer-error precondition,
/// so failure is a recoverable return, not an assert.
BK_GfxPipeline *bk_gfx_pipeline_create(const BK_GfxPipelineDesc *desc);

/// Destroys a pipeline. No-op if pipeline is nullptr.
void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline);
```

## 4. Frame integration — binding and drawing

PLAN.md 6.7 is explicit that the general record/flush draw-list architecture is P3
scope ("do not scaffold it"), so this sub-project does not introduce draw-list
recording. Instead it extends `bk_gfx` with a single pending bind+draw slot, the same
shape `bk_gfx_set_clear_color` already uses: the render callback sets state, and
`bk__gfx_flush` (already the sole owner of the render pass) consults it:

```c
// added to bk_gfx.h
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);
void bk_gfx_draw(int vertex_count);
```

`bk__gfx_flush` binds the pending pipeline and issues the draw between the existing
clear (`SDL_BeginGPURenderPass` with `LOADOP_CLEAR`) and `SDL_EndGPURenderPass`, then
clears the pending state for the next frame. `BK_AppDesc`'s `render` callback
signature in `bk_app.h` (already shipped in Phase 1) is unchanged — no command
buffer or render pass is exposed to user code. This is deliberately a single slot,
not a list: P3 replaces it with real multi-draw recording when the draw2d module
lands; sub-project 1 only needs one pipeline bound and one draw issued to prove the
pipeline object works end to end.

## 5. Sample — `samples/03_triangle`

Creates a pipeline from `shaders/triangle.vertex.hlsl` /
`shaders/triangle.fragment.hlsl` — a minimal pair where the vertex shader emits a
triangle from `SV_VertexID` (no vertex buffer, `num_vertex_buffers = 0`) and the
fragment shader outputs a solid or simple gradient color. Calls
`bk_gfx_bind_pipeline` and `bk_gfx_draw(3)` from its `render` callback. Same
`--frames N` convention as `01_clear`/`02_ticks` for CI smoke-testing.

## 6. Testing — `tests/test_gfx_pipeline.c`

A **golden-image test**, and it needs neither a window nor a swapchain: an
`SDL_GPUDevice` plus an offscreen `SDL_GPUTexture` created directly as a color target
is enough to run a full render pass headlessly. The test:

1. Creates a GPU device (no window).
2. Creates an offscreen color-target texture.
3. Creates the pipeline (same triangle shaders as the sample) with
   `color_target_format` matching the offscreen texture's format.
4. Runs one render pass rendering the triangle into the offscreen texture.
5. Downloads the pixels (`SDL_DownloadFromGPUTexture` via a transfer buffer + fence
   wait).
6. Hashes the raw bytes (FNV-1a) and compares against a checked-in expected constant.

This sidesteps the `xvfb-run`/lavapipe fragility Phase 1's on-screen sample testing
already carries in CI (see `PLAN.md` 6.10) — pipeline correctness is verifiable in CI
without a display server at all.

## 7. Error handling

Follows the existing `bk_app.c` boot-path convention: SDL_GPU call failures during
shader/pipeline creation log via `SDL_Log` with a `"BK: "` prefix and
`bk_gfx_pipeline_create` returns `nullptr`. `bk_gfx_pipeline_destroy(nullptr)` is a
no-op, matching `bk__free`'s style elsewhere in the codebase.

## 8. Explicitly out of scope for this sub-project

- **Compute pipelines, buffers, textures** — sub-project 2. A compute pipeline has
  nothing to dispatch against until storage buffers/textures exist.
- **Canvases/render targets, depth-stencil state** — sub-project 3. Nothing in this
  sub-project produces a depth target.
- **Resize handling** — nothing here owns a fixed-size target; the swapchain already
  adapts for free via SDL_GPU's default full-target viewport when no explicit
  `SDL_SetGPUViewport` is called. Revisit once canvases (fixed-size, window-relative
  targets) exist.
- **VFS/asset-pack shader loading** — loose files only, read directly by samples/tests
  via `SDL_LoadFile`; no `bk_` wrapper, no dependency on PhysFS/P6.
- **Full SDL_GPU blend-factor/op matrix** — `BK_GfxBlendMode` covers none/alpha only.

## 9. Decisions and rationale (do not relitigate in implementation sessions)

- Sub-project decomposition (shader+pipeline / buffers+textures+compute / canvases)
  over one large P2 spec: matches the "one module per session" discipline: three
  independently reviewable, independently implementable units.
- Graphics pipelines only this sub-project, compute deferred: no working sample is
  possible for compute until storage resources exist next sub-project.
- Binding/drawing is a single pending bind+draw slot on `bk_gfx` (`bk_gfx_bind_pipeline`
  + `bk_gfx_draw`), not exposing the render pass to `render()` and not a draw list:
  PLAN.md explicitly reserves general draw-list recording for P3, and this slot
  requires no change to `bk_app.h`'s already-shipped `BK_AppDesc.render` signature.
- Shader bytecode reaches the pipeline as loose files loaded by the caller, not
  through a VFS stub or `#embed`: the asset-pack pipeline is P6 scope and `#embed` is
  explicitly reserved for later phases per `CLAUDE.md`.
- Shader descriptors carry all three precompiled variants (spv/dxil/msl) and
  `bk_gfx_pipeline_create` picks the one matching the device's supported formats,
  rather than pushing format selection to every call site: this logic will otherwise
  be duplicated by every future pipeline-creating module (P3 draw2d, P5 text, P10
  ImGui backend). Matches the shape CF uses for its own shader tables.
- No persistent `BK_GfxShader` handle: SDL_GPU's own object model creates, uses in
  pipeline creation, and releases a shader immediately — nothing re-references it
  afterward, so a persistent handle has no reuse case to justify it.
- `color_target_format` is an explicit caller-supplied `SDL_GPUTextureFormat`, not a
  `bk_` wrapper around `SDL_GetGPUSwapchainTextureFormat`: avoids wrapping an SDL call
  that already does the job, and needs no change when canvases add a second kind of
  target in sub-project 3.
- `BK_GfxBlendMode` is a two-value enum, not SDL_GPU's full blend-factor/op struct:
  2D work needs alpha blending or nothing; more modes are added when a real use case
  demands them, not speculatively.
