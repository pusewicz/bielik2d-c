# Bielik2D — Phase 2, Sub-project 2: Buffers, Textures, and Compute Pipelines

## 0. Context and scope

`PLAN.md` §7 scopes Phase 2 ("P2 gfx core") as: pipelines, buffers, textures, offline
shader compile, canvases/render targets. Sub-project 1 (shader toolchain + graphics
pipelines) shipped that first slice and its spec
(`docs/superpowers/specs/2026-07-29-phase2-shader-pipeline-design.md` §0) decomposed
the rest into:

1. Shader toolchain + graphics pipelines — done.
2. **This spec**: buffers + textures + compute pipelines.
3. Canvases/render targets + resize handling + depth-stencil.

Sub-project 1 explicitly deferred compute here: "a compute pipeline has nothing to
dispatch against until storage buffers/textures exist" (spec §8). Those prerequisites
are what this sub-project builds, plus vertex/index buffers and sampled textures —
`bk_gfx_pipeline` can currently only draw geometry a vertex shader invents from
`gl_VertexIndex` with a flat fragment color; there is no way to hand the GPU real data.
This is also the direct prerequisite for P3's sprite batch (a vertex buffer plus an
atlas texture).

Ships as one branch, one PR, sequenced internally as 2a (buffers + textures, proven end
to end by a textured-quad sample) then 2b (compute pipelines, proven by a compute
sample), with a review checkpoint at the 2a/2b boundary.

## 1. Module boundaries and file layout

Two new modules, plus compute pipeline support folded into the existing
`bk_gfx_pipeline` module (see §9 for why):

```
bielik2d/
  shaders/
    textured.vert / textured.frag      (new)
    gradient.comp                      (new)
  include/bielik/
    bk_gfx_buffer.h                    (new)
    bk_gfx_texture.h                   (new)
    bk_gfx_pipeline.h                  (modified: compute types + functions)
  src/
    bk_gfx_buffer.c                    (new)
    bk_gfx_texture.c                   (new)
    bk_gfx_pipeline.c                  (modified: compute create/destroy/dispatch)
    internal/
      bk_gfx_buffer_internal.h         (new)
      bk_gfx_texture_internal.h        (new)
  samples/
    04_textured_quad/main.c            (new)
    05_compute/main.c                  (new)
  tests/
    test_gfx_buffer.c                  (new)
    test_gfx_texture.c                 (new; also holds the textured-quad golden-image test)
    test_gfx_compute.c                 (new)
```

`bk_gfx.h`/`bk_gfx.c` (frame flush, pending bind/draw slot) are extended, not replaced:
the existing single pending-slot design from sub-project 1 grows more slots (vertex
buffer, index buffer, texture+sampler) rather than becoming a draw list — `PLAN.md` §8
still reserves general draw-list recording for P3.

## 2. Shader toolchain: adding the `compute` stage

`cmake/shaders.cmake`'s `bk_compile_shader` currently accepts only `STAGE vertex` or
`STAGE fragment` and `FATAL_ERROR`s otherwise. Add `STAGE compute`, mapping to
`-fshader-stage=compute` and source extension `.comp`. No other changes to the
toolchain: the same `glslc` (GLSL → SPIR-V) → `spirv-cross` (SPIR-V → MSL) pipeline
already in place, same committed-bytecode policy (regeneration is `find_program`-gated
and optional; committed `.spv`/`.msl` files are what ships).

New GLSL sources:

- `shaders/textured.vert` — takes `position` (vec2), `uv` (vec2), `color` (vec4,
  unpacked from `ubyte4_norm`), outputs clip-space position and passes uv/color
  through.
- `shaders/textured.frag` — samples a `combined image sampler` at `set = 2, binding =
  0` (SDL's documented fragment-shader SPIR-V resource-set convention — see §3),
  multiplies by the vertex color.
- `shaders/gradient.comp` — local size `8x8x1`; reads a `set = 0, binding = 0`
  read-only storage buffer (a small per-run parameter array — see §7), writes a `set =
  1, binding = 0` `rgba8` `image2D`.

## 3. MSL resource binding — the load-bearing constraint

SDL_GPU's resource-binding convention differs by target format
(`SDL_CreateGPUShader`'s documentation, `SDL_gpu.h`):

- **SPIR-V**, graphics: vertex shaders use set 0 (textures/storage) + set 1 (uniform
  buffers); fragment shaders use set 2 (textures/storage) + set 3 (uniform buffers).
  Compute: set 0 (sampled/storage textures, storage buffers with read access) + set 1
  (read-write storage textures/buffers) + set 2 (uniform buffers).
- **MSL**: `[[texture]]` = sampled textures then storage textures, `[[sampler]]` =
  indices matching the sampled textures, `[[buffer]]` = uniform buffers then storage
  buffers (vertex buffer 0 lands at `[[buffer(14)]]`, buffer 1 at `[[buffer(15)]]`,
  etc. — use `[[stage_in]]` rather than hand-authoring vertex buffer indices).

`spirv-cross` assigns MSL binding indices by its own internal scheme when
cross-compiling from SPIR-V, which is not guaranteed to match SDL's ordering,
particularly for the compute shader's mixed read-only-buffer / read-write-texture
resource set. Since the primary dev machine targets Metal, a mismatch here is a
correctness bug that would only surface at `SDL_CreateGPUShader`/pipeline-creation
time with an opaque validation failure, not a compile error.

**Verification gate, run after generating each shader's `.msl`:**

```bash
grep -E '\[\[(texture|sampler|buffer)\(' shaders/textured.fragment.msl shaders/gradient.compute.msl
```

Check the emitted indices against the ordering above. If `spirv-cross` didn't already
produce a match, fix with its MSL binding-remap flags (`--msl-decoration-binding` or
the per-resource `--msl-*-binding` family) applied to the specific offending
resource, chosen against the real emitted output — not guessed in advance. Any remap
flags used get added to `cmake/shaders.cmake`'s `bk_compile_shader` invocation for the
affected shader and recorded in `DEVIATIONS.md` alongside the existing
glslc/spirv-cross entry.

## 4. Public API — `bk_gfx_buffer.h`

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <stdint.h>

/// What a buffer is used for. Exclusive, not a bitmask -- SDL_GPU itself rejects a
/// buffer created with both VERTEX and INDEX usage, and nothing in 2D needs a buffer
/// that is more than one thing at once.
typedef enum BK_GfxBufferUsage {
    BK_GFX_BUFFER_USAGE_VERTEX,
    BK_GFX_BUFFER_USAGE_INDEX,         // 16-bit indices; see decision in §9
    BK_GFX_BUFFER_USAGE_STORAGE_READ,  // read-only storage buffer in a compute shader
} BK_GfxBufferUsage;

/// A GPU buffer: vertex data, index data, or compute storage input. Owns its device
/// upload machinery internally; callers just create, upload, bind (via bk_gfx), and
/// destroy.
typedef struct BK_GfxBuffer BK_GfxBuffer;

/// Creates a buffer of the given usage and byte size. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on SDL_GPU failure. device is explicit, matching
/// bk_gfx_pipeline_create's precedent -- testable with no window/app.
BK_GfxBuffer *bk_gfx_buffer_create(SDL_GPUDevice *device, BK_GfxBufferUsage usage,
                                   uint32_t size);

/// Uploads size bytes from data into buffer starting at byte offset offset. Returns
/// false and logs via SDL_Log if offset + size exceeds the buffer's size (a
/// runtime-data-dependent failure, not a programmer-error precondition -- same
/// convention as bk_gfx_pipeline_create's vertex-buffer/attribute count checks).
/// buffer and data must be non-null (BK_ASSERT).
bool bk_gfx_buffer_upload(BK_GfxBuffer *buffer, const void *data, uint32_t offset,
                          uint32_t size);

/// Destroys a buffer. No-op if buffer is nullptr.
void bk_gfx_buffer_destroy(BK_GfxBuffer *buffer);
```

## 5. Public API — `bk_gfx_texture.h`

```c
#pragma once
#include <SDL3/SDL_gpu.h>

/// What a texture is used for.
typedef enum BK_GfxTextureUsage {
    BK_GFX_TEXTURE_USAGE_SAMPLER,         // CPU-uploaded, sampled by a fragment shader
    BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET,  // written by a compute shader, then sampled
} BK_GfxTextureUsage;

typedef enum BK_GfxFilter {
    BK_GFX_FILTER_NEAREST,
    BK_GFX_FILTER_LINEAR,
} BK_GfxFilter;

typedef enum BK_GfxAddressMode {
    BK_GFX_ADDRESS_CLAMP,
    BK_GFX_ADDRESS_REPEAT,
} BK_GfxAddressMode;

/// An R8G8B8A8_UNORM 2D texture -- the only format this sub-project needs (a
/// sprite/atlas path). More formats get added when a real use case demands them.
typedef struct BK_GfxTexture BK_GfxTexture;

/// A sampler: filtering + addressing state, bound alongside a texture at draw time.
typedef struct BK_GfxSampler BK_GfxSampler;

/// Creates a width x height R8G8B8A8_UNORM texture for the given usage. Logs via
/// SDL_Log and returns nullptr on SDL_GPU failure.
BK_GfxTexture *bk_gfx_texture_create(SDL_GPUDevice *device, BK_GfxTextureUsage usage,
                                     int width, int height);

/// Uploads width*height RGBA8 pixels (tightly packed, 4 bytes/pixel) into texture.
/// Only valid for a BK_GFX_TEXTURE_USAGE_SAMPLER texture -- BK_ASSERTs otherwise, since
/// uploading into a compute-write target is a programmer error, not a runtime
/// condition. Returns false and logs on SDL_GPU failure.
bool bk_gfx_texture_upload(BK_GfxTexture *texture, const void *rgba_pixels);

/// Destroys a texture. No-op if texture is nullptr.
void bk_gfx_texture_destroy(BK_GfxTexture *texture);

/// Creates a sampler with the given filter and addressing mode (applied to both U and
/// V). Logs via SDL_Log and returns nullptr on SDL_GPU failure.
BK_GfxSampler *bk_gfx_sampler_create(SDL_GPUDevice *device, BK_GfxFilter filter,
                                     BK_GfxAddressMode address_mode);

/// Destroys a sampler. No-op if sampler is nullptr.
void bk_gfx_sampler_destroy(BK_GfxSampler *sampler);
```

## 6. Public API additions — `bk_gfx_pipeline.h` (compute)

```c
/// One compute shader, precompiled to all three backend formats, plus its resource
/// counts (mirrors BK_GfxShaderDesc's shape, but compute's read-only-buffer /
/// read-write-texture resource kinds are distinct from a graphics shader's).
typedef struct BK_GfxComputePipelineDesc {
    BK_GfxShaderVariant spirv;
    BK_GfxShaderVariant dxil;
    BK_GfxShaderVariant msl;
    int num_readonly_storage_buffers;
    int num_readwrite_storage_textures;
    uint32_t threadcount_x, threadcount_y, threadcount_z;  // must match the shader's
                                                             // local_size_{x,y,z}
} BK_GfxComputePipelineDesc;

/// Opaque compute pipeline.
typedef struct BK_GfxComputePipeline BK_GfxComputePipeline;

/// Creates a compute pipeline. Logs via SDL_Log and returns nullptr on SDL_GPU
/// failure. device is explicit, same rationale as bk_gfx_pipeline_create.
BK_GfxComputePipeline *bk_gfx_compute_pipeline_create(SDL_GPUDevice *device,
                                                      const BK_GfxComputePipelineDesc *desc);

/// Destroys a compute pipeline. No-op if pipeline is nullptr.
void bk_gfx_compute_pipeline_destroy(BK_GfxComputePipeline *pipeline);

/// Describes one dispatch: which pipeline, which resources bound to it (slot order
/// matches the arrays), and the workgroup counts.
typedef struct BK_GfxComputeDispatchDesc {
    BK_GfxComputePipeline *pipeline;
    BK_GfxTexture *const *readwrite_textures;
    int num_readwrite_textures;
    BK_GfxBuffer *const *readonly_buffers;
    int num_readonly_buffers;
    uint32_t groups_x, groups_y, groups_z;
} BK_GfxComputeDispatchDesc;

/// Dispatches a compute pipeline synchronously: acquires its own command buffer,
/// records the dispatch, submits, and blocks on a GPU fence until it completes.
/// Intended for setup-time work (e.g. procedurally filling a texture once at init),
/// not a per-frame call -- unlike bk_gfx_draw, there is no pending-slot/flush
/// integration, because compute work has no natural "once per frame" cadence the way
/// the render pass does. Returns false and logs on SDL_GPU failure.
bool bk_gfx_compute_dispatch(const BK_GfxComputeDispatchDesc *desc);
```

`bk_gfx_pipeline_create`'s internal shader-variant picker
(`s_pick_shader_variant`/`s_create_shader` in `src/bk_gfx_pipeline.c`) is refactored to
return `const BK_GfxShaderVariant *` given the three candidate variants, dropping four
out-parameters, so both the existing graphics-shader path and the new compute-shader
path share it without duplication.

## 7. Frame integration — extended pending slots

`bk_gfx.h` gains binding calls that extend the existing single pending-slot design
(sub-project 1 spec §4), not a draw list:

```c
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);
void bk_gfx_draw_indexed(int index_count);
```

One slot each (slot 0 only for every binding — multi-slot draw calls are P3's
problem). `bk__gfx_flush` (`src/bk_gfx.c`) already snapshots and clears all pending
state at the top of the function before acquiring the swapchain texture; this task
extends that same snapshot/clear block and the render-pass body: bind vertex buffer at
slot 0, bind index buffer (hardcoded `SDL_GPU_INDEXELEMENTSIZE_16BIT` — see §9), bind
texture+sampler at fragment slot 0, then `SDL_DrawGPUIndexedPrimitives`.

Compute dispatch does **not** go through this pending-slot/flush mechanism: it's
synchronous and immediate (§6), driven directly by the caller (typically once, in
`init`), not queued for the next frame's flush.

## 8. Testing

Each new module gets a `bk_test.h`-style unit test file (create/upload/destroy, bad
input handling, `destroy(nullptr)` no-op) following the existing `tests/test_gfx.c` /
`tests/test_gfx_pipeline.c` pattern, plus a `test_header_bk_gfx_{buffer,texture}.c`
standalone-compile stub per new public header.

**Golden-image test (in `tests/test_gfx_texture.c`)**: a headless, no-window,
no-swapchain test that proves buffers, textures, samplers, and indexed drawing work
together — an uploaded vertex buffer (position/uv/color), an index buffer describing
two triangles forming a quad, and a 2×2 checkerboard texture sampled with `NEAREST`,
rendered into an offscreen color target and checked at known pixel coordinates within a
tolerance (never a whole-buffer hash — see sub-project 1 spec §6 for why: backends
don't rasterize bit-identically). Expected colors in the assertions are **hardcoded
literals**, independent of the checkerboard array the test uploads, so a mutation test
(temporarily change one uploaded texel, confirm the assertion now fails, then revert)
actually discriminates rather than trivially passing against a self-derived expectation.

**Compute test (`tests/test_gfx_compute.c`)**: dispatches `gradient.comp` against a
small read-only storage buffer and a write-target texture, then reads the texture back
with the existing `bk__gfx_download_texture` helper (`src/bk_gfx.c`, already used by
`tests/test_gfx_pipeline.c`'s golden-image test and by frame capture) and checks known
pixels. No new download plumbing needed.

## 9. Decisions and rationale (do not relitigate in implementation sessions)

- **Compute lives in `bk_gfx_pipeline`, not a fourth module**: it reuses the
  shader-variant-picking logic every pipeline-creating module needs (the same
  SPIR-V/DXIL/MSL selection `bk_gfx_pipeline_create` already does), and a compute
  pipeline is conceptually "the other kind of SDL_GPU pipeline" rather than a
  standalone resource type the way a buffer or texture is.
- **No shared transfer/upload helper across buffer and texture modules**:
  `SDL_UploadToGPUBuffer` and `SDL_UploadToGPUTexture` take different region
  structs (`SDL_GPUBufferRegion` vs. `SDL_GPUTextureRegion`/`SDL_GPUTextureTransferInfo`);
  unifying them behind one internal helper needs a tagged union to save roughly 25
  lines total. Each module keeps its own small `s_upload` static instead.
  `bk__gfx_download_texture` stays in `src/bk_gfx.c` where it already lives —
  `CLAUDE.md` forbids reorganizing the file layout beyond `PLAN.md` §4.
- **Buffer usage is an exclusive enum, not a bitmask**: `SDL_CreateGPUBuffer`'s
  documentation calls out `VERTEX | INDEX` as an explicitly invalid combination, and
  2D drawing never needs a buffer serving two roles simultaneously.
- **Index buffers are 16-bit, hardcoded** in `bk__gfx_flush`'s index-buffer bind (not
  exposed as a parameter): `SDL_BindGPUIndexBuffer` takes an
  `SDL_GPUIndexElementSize`, but nothing in `bk_gfx_bind_index_buffer`'s signature
  carries it, so the framework picks `_16BIT` unconditionally. A 2D sprite batch
  never approaches 65k vertices in one draw call. Revisit only if a real use case
  needs more.
- **`bk_gfx_buffer_upload` bounds failures return `false`, they don't assert**:
  matches the precedent `bk_gfx_pipeline_create`'s `num_vertex_buffers`/
  `num_vertex_attributes` range checks already set (`src/bk_gfx_pipeline.c`, tested by
  `tests/test_gfx_pipeline.c`'s `test_out_of_range_vertex_counts_return_null`) — an
  oversized upload is caller-supplied runtime data, not a fixed programmer-error
  precondition, so it's a recoverable failure, not `BK_ASSERT`. Null `buffer`/`data`
  stay assertions. The bounds comparison is overflow-safe (`size > buffer_size -
  offset`, since `offset + size` can wrap a `uint32_t`).
- **Texture format is fixed to `R8G8B8A8_UNORM`** this sub-project: the only format a
  sprite/atlas path needs. More formats (compressed, HDR) get added when a real use
  case demands them, matching the repo's "no speculative options" rule.
- **Uniform buffers / push constants are explicitly out of scope**, deferred to P3.
  The textured-quad sample hardcodes NDC-space positions directly in its uploaded
  vertex buffer; the compute shader hardcodes its target dimensions. Adding a uniform
  path with no consumer yet would be exactly the speculative option the repo's
  conventions rule out.
- **Compute dispatch is synchronous and immediate, not integrated into the frame
  pending-slot/flush mechanism**: the render pass has a natural once-per-frame
  cadence that `bk_gfx_bind_pipeline`/`bk_gfx_draw` hook into; compute work (typically
  one-time procedural generation) does not. `bk_gfx_compute_dispatch` takes the
  device explicitly and manages its own command buffer/fence, mirroring
  `bk_gfx_pipeline_create`'s testability rationale (works with no window, no running
  app) and `bk__gfx_download_texture`'s existing submit-and-fence-wait pattern.

## 10. Explicitly out of scope for this sub-project

- **Canvases/render targets, depth-stencil, resize handling** — sub-project 3.
- **Uniform buffers / push constants** — no consumer exists yet in this sub-project's
  scope (see §9); deferred to P3, which needs them for per-sprite transforms.
- **Multiple simultaneous vertex buffer slots, multiple bound textures** — one slot
  each; P3's sprite batch design decides the real multi-resource shape.
- **Texture formats other than `R8G8B8A8_UNORM`.**
- **Mipmapping, texture arrays, cubemaps, 3D textures.**
- **Draw-list recording of any kind** — `PLAN.md` §8 reserves this for P3.
- **Async/queued compute dispatch** — this sub-project's dispatch is synchronous only.
