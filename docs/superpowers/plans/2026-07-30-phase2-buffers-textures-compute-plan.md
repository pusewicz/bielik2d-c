# Phase 2 Sub-project 2: Buffers, Textures, and Compute Pipelines — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `bk_gfx_buffer` and `bk_gfx_texture` modules, extend `bk_gfx_pipeline`
with compute pipeline create/destroy/dispatch, extend `bk_gfx`'s pending-slot with
vertex/index buffer and texture binding, and ship two working samples
(`04_textured_quad`, `05_compute`) proving the whole chain end to end.

**Architecture:** Two new resource modules (buffer, texture) following the existing
`bk_gfx_pipeline` shape: explicit-device create, runtime-failure-returns-null/false,
`BK_ASSERT` for programmer-error preconditions. Compute pipelines extend
`bk_gfx_pipeline` rather than becoming a fourth module, reusing its shader-variant
picker. `bk_gfx`'s existing single pending bind+draw slot (sub-project 1) grows more
slots; it does not become a draw list. Full rationale and the public API in full is in
the spec: `docs/superpowers/specs/2026-07-30-phase2-buffers-textures-compute-design.md`
— read it before starting, this plan implements it exactly, including its binding-gate
requirements.

**Tech Stack:** C23, SDL3 GPU API, GLSL (`glslc`/`spirv-cross`), CMake, CTest.

## Global Constraints

- Spec of record: `docs/superpowers/specs/2026-07-30-phase2-buffers-textures-compute-design.md`.
- Naming: public functions `bk_` + snake_case, public types `BK_` + PascalCase, enum
  values `BK_` + UPPER_SNAKE, internal linker-visible symbols `bk__` prefix,
  file-static functions `s_` prefix.
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).
- `.clang-format`: LLVM base, 4-space indent, 100 columns, `PointerAlignment: Right`,
  K&R attached braces. Run `clang-format -i` on every file you touch before
  committing.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant — copy verbatim from the spec's header
  listings (§4-§6), which are normative.
- Errors: recoverable runtime failures (bad bytecode, unsupported format, oversized
  upload) log via `SDL_Log` with a `"BK: "` prefix and return `nullptr`/`false` — they
  do NOT assert. Programmer-error preconditions (null arguments) use `BK_ASSERT`.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`.
  Test: `ctest --test-dir build --output-on-failure`.
- Commit style: atomic commits, human-voice messages (no Conventional Commits
  prefixes, no AI signoff/Co-Authored-By footers).
- `#embed` is reserved for later phases — do not use it.

---

## Task 1: Compute shader stage in the toolchain + `textured`/`gradient` shaders

**Files:**
- Modify: `cmake/shaders.cmake` (add `STAGE compute`)
- Modify: `CMakeLists.txt` (register the three new `bk_compile_shader` calls)
- Create: `shaders/textured.vert`, `shaders/textured.frag`, `shaders/gradient.comp`
- Create (committed bytecode): `shaders/textured.vertex.{spv,msl}`,
  `shaders/textured.fragment.{spv,msl}`, `shaders/gradient.compute.{spv,msl}`
- Modify: `DEVIATIONS.md` (only if the MSL gate requires binding-remap flags)

No C code in this task — verify each step's output as you go.

- [ ] **Step 1: Add `compute` stage support to `cmake/shaders.cmake`**

In `bk_compile_shader`'s stage `if/elseif` chain, add:

```cmake
elseif(ARG_STAGE STREQUAL "compute")
    set(glslc_stage "compute")
    set(src_ext "comp")
```

- [ ] **Step 2: Write `shaders/textured.vert`**

```glsl
#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    out_uv = in_uv;
    out_color = in_color;
}
```

- [ ] **Step 3: Write `shaders/textured.frag`**

Per SDL's SPIR-V fragment-shader convention (set 2 = textures, set 3 = uniform
buffers — see spec §3):

```glsl
#version 450

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(tex, in_uv) * in_color;
}
```

- [ ] **Step 4: Write `shaders/gradient.comp`**

Per SDL's SPIR-V compute convention (set 0 = read-access resources, set 1 =
read-write resources — see spec §3). The storage buffer holds a small fixed-size
parameter array (e.g. two `vec4`s: a base color and a scale) so the shader has
something real to read without needing uniform buffers:

```glsl
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, set = 0, binding = 0) readonly buffer Params {
    vec4 base_color;
    vec4 scale;
} params;

layout(set = 1, binding = 0, rgba8) uniform writeonly image2D out_image;

void main() {
    ivec2 size = imageSize(out_image);
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= size.x || coord.y >= size.y) {
        return;
    }
    vec2 uv = vec2(coord) / vec2(size - ivec2(1));
    vec4 color = params.base_color + params.scale * vec4(uv, 0.0, 0.0);
    imageStore(out_image, coord, color);
}
```

- [ ] **Step 5: Compile and cross-compile all three, verify output**

```bash
glslc -fshader-stage=vertex shaders/textured.vert -o shaders/textured.vertex.spv
glslc -fshader-stage=fragment shaders/textured.frag -o shaders/textured.fragment.spv
glslc -fshader-stage=compute shaders/gradient.comp -o shaders/gradient.compute.spv
spirv-cross --msl shaders/textured.vertex.spv --output shaders/textured.vertex.msl
spirv-cross --msl shaders/textured.fragment.spv --output shaders/textured.fragment.msl
spirv-cross --msl shaders/gradient.compute.spv --output shaders/gradient.compute.msl
```

Expected: all six commands exit 0, all six output files exist with non-zero size.
Confirm each `.msl` file's entry point is named `main0` (spirv-cross's rename), and
each `.spv`'s SPIR-V entry point is still `main` (`spirv-cross <file>.spv --reflect |
grep '"name"'`).

- [ ] **Step 6: Run the MSL resource-binding gate (spec §3) — do not skip**

```bash
grep -E '\[\[(texture|sampler|buffer)\(' shaders/textured.fragment.msl shaders/gradient.compute.msl
```

Check against SDL's documented ordering (spec §3): fragment shader's sampled
texture/sampler should be index 0 each; compute's read-only storage buffer and
read-write storage image should land in the `[[buffer]]`/`[[texture]]` slots SDL
expects (uniform-then-storage-buffers, sampled-then-storage-textures). If any index is
wrong, apply the narrowest `spirv-cross` MSL binding-remap flag
(`--msl-decoration-binding` or a `--msl-*-binding` variant) to the offending resource,
re-run Step 5's `spirv-cross` commands for that shader, and re-check. Record the exact
flags used in `DEVIATIONS.md` (new entry, same format as the existing
glslc/spirv-cross entry) and fold them into `cmake/shaders.cmake` so regeneration
stays reproducible.

- [ ] **Step 7: Register the shaders in `CMakeLists.txt`**

After the existing `bk_compile_shader(NAME triangle STAGE fragment)` line, add:

```cmake
bk_compile_shader(NAME textured STAGE vertex)
bk_compile_shader(NAME textured STAGE fragment)
bk_compile_shader(NAME gradient STAGE compute)
```

- [ ] **Step 8: Verify regeneration is deterministic**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build --target bk_shader_textured_vertex bk_shader_textured_fragment bk_shader_gradient_compute
git status shaders/
```

Expected: targets build, `git status shaders/` shows no diff versus what Step 5/6
committed.

- [ ] **Step 9: Commit**

```bash
git add shaders/ cmake/shaders.cmake CMakeLists.txt DEVIATIONS.md
git commit -m "add the compute shader stage and the textured-quad/gradient shaders"
```

(Omit `DEVIATIONS.md` from the add if Step 6 needed no remap flags.)

---

## Task 2: `bk_gfx_buffer` module

**Files:**
- Create: `include/bielik/bk_gfx_buffer.h` (copy verbatim from spec §4)
- Create: `src/bk_gfx_buffer.c`
- Create: `src/internal/bk_gfx_buffer_internal.h` (one accessor:
  `SDL_GPUBuffer *bk__gfx_buffer_handle(const BK_GfxBuffer *buffer)`, and
  `uint32_t bk__gfx_buffer_size(const BK_GfxBuffer *buffer)` for Task 5's bounds-free
  binding and Task 4's test)
- Create: `tests/test_gfx_buffer.c`, `tests/test_header_bk_gfx_buffer.c`
- Modify: `CMakeLists.txt` (add `src/bk_gfx_buffer.c` to `add_library(bielik ...)`)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the header, internal header, and a stub `.c`**

Header text is normative in spec §4 — copy it in. Stub `src/bk_gfx_buffer.c` contains
just `#include "internal/bk_gfx_buffer_internal.h"` so the test fails at link time.

- [ ] **Step 2: Write the failing test `tests/test_gfx_buffer.c`**

Cover, following `tests/test_gfx_pipeline.c`'s device-setup pattern
(`SDL_Init(SDL_INIT_VIDEO)` then `SDL_CreateGPUDevice`, no window):

- `bk_gfx_buffer_create` for each of the three usages succeeds (non-null).
- `bk_gfx_buffer_upload` with in-bounds data succeeds (returns `true`).
- `bk_gfx_buffer_upload` at a non-zero offset within bounds succeeds.
- `bk_gfx_buffer_upload` where `offset + size > buffer size` returns `false` (test the
  overflow-safe boundary too: `offset = buffer_size`, `size = 1`, and separately
  `offset` near `UINT32_MAX` with a small `size`, both must return `false` without
  UB).
- `bk_gfx_buffer_destroy(nullptr)` is a no-op.

Wire into `tests/CMakeLists.txt` (mirror the `test_gfx_pipeline` block, no
`bk_stage_shaders` needed — this module doesn't load shader bytecode) and confirm it
fails to build (undefined symbols).

- [ ] **Step 3: Implement `src/bk_gfx_buffer.c`**

`BK_GfxBuffer` struct holds `SDL_GPUDevice *device`, `SDL_GPUBuffer *handle`,
`uint32_t size`. `bk_gfx_buffer_create` maps `BK_GfxBufferUsage` to
`SDL_GPU_BUFFERUSAGE_{VERTEX,INDEX,COMPUTE_STORAGE_READ}` and calls
`SDL_CreateGPUBuffer`. `bk_gfx_buffer_upload` creates a transfer buffer sized to
`size`, maps it, `SDL_memcpy`s `data` in, unmaps, then copies via
`SDL_BeginGPUCopyPass`/`SDL_UploadToGPUBuffer`/`SDL_EndGPUCopyPass` into `buffer` at
byte `offset`, submits, and releases the transfer buffer — no fence wait needed for an
upload (unlike a download, nothing reads the result on the CPU side). Bounds check
first: `if (size > buffer->size - offset) { log; return false; }` — compute
`buffer->size - offset` only after confirming `offset <= buffer->size` (also
false-and-log if not), so the subtraction itself never wraps.

- [ ] **Step 4: Run the test, verify green**

```bash
cmake --build build --target test_gfx_buffer && ./build/tests/test_gfx_buffer
```

- [ ] **Step 5: Full suite + format check, then commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx_buffer.h src/bk_gfx_buffer.c src/internal/bk_gfx_buffer_internal.h tests/test_gfx_buffer.c tests/test_header_bk_gfx_buffer.c
git add include/bielik/bk_gfx_buffer.h src/bk_gfx_buffer.c src/internal/bk_gfx_buffer_internal.h tests/test_gfx_buffer.c tests/test_header_bk_gfx_buffer.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "add the bk_gfx_buffer module"
```

---

## Task 3: `bk_gfx_texture` module

**Files:**
- Create: `include/bielik/bk_gfx_texture.h` (copy verbatim from spec §5)
- Create: `src/bk_gfx_texture.c`
- Create: `src/internal/bk_gfx_texture_internal.h`
  (`SDL_GPUTexture *bk__gfx_texture_handle(const BK_GfxTexture *texture)`,
  `SDL_GPUSampler *bk__gfx_sampler_handle(const BK_GfxSampler *sampler)`, both consumed
  by Task 5's binding and Task 4/7's tests)
- Create: `tests/test_gfx_texture.c`, `tests/test_header_bk_gfx_texture.c`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the header, internal header, stub `.c`**

- [ ] **Step 2: Write the failing unit tests**

In `tests/test_gfx_texture.c`: create a `BK_GFX_TEXTURE_USAGE_SAMPLER` texture,
upload pixels, destroy; create a `BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET` texture
(**this is the usage-flag gate from spec §5/plan checklist — confirm
`SDL_CreateGPUTexture` actually succeeds with `SAMPLER | COMPUTE_STORAGE_WRITE`**; if
it fails, stop and re-read spec §9/§10 before proceeding — the compute sample's design
depends on this); `bk_gfx_texture_upload` on a compute-target texture must fail an
assertion (BK_ASSERT — test via a death-test pattern only if the repo already has one,
otherwise skip asserting the crash and just don't call it that way in tests); create
samplers for both filter/address-mode combinations; `destroy(nullptr)` no-ops for both
types.

- [ ] **Step 3: Also verify the compute storage-write format assumption here**

Before Task 7 builds on it: create a `BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET` texture and
confirm `SDL_CreateGPUTexture` succeeds with `R8G8B8A8_UNORM` +
`SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE` on this machine's default backend
(Metal). This is a real test assertion, not just manual verification — fold it into
Step 2's compute-target test.

- [ ] **Step 4: Implement `src/bk_gfx_texture.c`**

`BK_GfxTexture` holds `device`, `handle`, `width`, `height`, `usage`.
`bk_gfx_texture_create` maps usage to `SDL_GPU_TEXTUREUSAGE_SAMPLER` or
`SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE`
respectively (a compute target must still be sampler-usable so the later draw can read
it — that's the whole point of `05_compute`), format `R8G8B8A8_UNORM`, type `2D`.
`bk_gfx_texture_upload`: `BK_ASSERT(texture->usage == BK_GFX_TEXTURE_USAGE_SAMPLER)`,
then transfer-buffer-map-memcpy-unmap-copy-pass pattern (parallel to
`bk__gfx_download_texture`'s upload-direction mirror image). `bk_gfx_sampler_create`
maps `BK_GfxFilter`/`BK_GfxAddressMode` to `SDL_GPUSamplerCreateInfo` fields
(`min_filter`/`mag_filter`, `address_mode_u`/`address_mode_v`) and calls
`SDL_CreateGPUSampler`.

- [ ] **Step 5: Run, verify green; full suite + format check; commit**

```bash
cmake --build build --target test_gfx_texture && ./build/tests/test_gfx_texture
cmake --build build && ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx_texture.h src/bk_gfx_texture.c src/internal/bk_gfx_texture_internal.h tests/test_gfx_texture.c tests/test_header_bk_gfx_texture.c
git add include/bielik/bk_gfx_texture.h src/bk_gfx_texture.c src/internal/bk_gfx_texture_internal.h tests/test_gfx_texture.c tests/test_header_bk_gfx_texture.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "add the bk_gfx_texture module"
```

---

## Task 4: Headless golden-image test — textured, indexed quad

**Files:**
- Modify: `tests/test_gfx_texture.c`

Proves buffers + textures + samplers + indexed drawing work together, the same way
sub-project 1's golden-image test proved the pipeline alone worked.

- [ ] **Step 1: Add the test**

Headless device (no window), matching `tests/test_gfx_pipeline.c`'s
`test_draw_produces_expected_pixels` structure:

1. Create device, load `textured.{vertex,fragment}.{spv,msl}` via `SDL_LoadFile` (same
   `s_load_shader_file`/`s_load_triangle_shader`-shaped helpers, renamed for
   `textured`), build the pipeline with `num_vertex_buffers = 1`,
   `num_vertex_attributes = 3` (position `FLOAT2` @ offset 0, uv `FLOAT2` @ offset 8,
   color `UBYTE4_NORM` @ offset 16; pitch 20).
2. `bk_gfx_buffer_create`/`upload` a vertex buffer: 4 vertices forming a full-viewport
   quad in NDC (`[-1,-1]`..`[1,1]`), uv `[0,0]`..`[1,1]`, color all-white
   (`0xFFFFFFFF` packed).
3. `bk_gfx_buffer_create`/`upload` an index buffer: `{0,1,2, 2,1,3}` or equivalent
   two-triangle winding for the quad (`uint16_t`).
4. `bk_gfx_texture_create` (`SAMPLER`, 2x2) + `bk_gfx_texture_upload` a hardcoded 2×2
   checkerboard: e.g. `(0,0)`=red, `(1,0)`=green, `(0,1)`=blue, `(1,1)`=white — four
   maximally-distinguishable literal colors. `bk_gfx_sampler_create(NEAREST, CLAMP)`
   (NEAREST is required — see spec §8, backend-stable sampling with no interpolation
   ambiguity).
5. Offscreen color target (same 64×64 `R8G8B8A8_UNORM` pattern as
   `test_gfx_pipeline.c`), begin render pass, bind pipeline, bind vertex buffer (`
   SDL_BindGPUVertexBuffers`), bind index buffer (`SDL_BindGPUIndexBuffer`,
   `_16BIT`), bind texture+sampler as a fragment sampler
   (`SDL_BindGPUFragmentSamplers`), `SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0,
   0)`, end pass.
6. Download via `bk__gfx_download_texture`, check four quadrant-center pixels against
   the four **hardcoded literal** checkerboard colors from step 4 (not re-derived from
   the array uploaded in step 4 — see spec §8's mutation-test rationale) within a
   small tolerance.

- [ ] **Step 2: Run, verify green**

```bash
cmake --build build --target test_gfx_texture && ./build/tests/test_gfx_texture
```

- [ ] **Step 3: Mutation-test — confirm the test actually discriminates**

Temporarily change one checkerboard texel's uploaded color in the test source (e.g.
swap the red quadrant to yellow) without touching the hardcoded expected-pixel
literals. Rebuild and rerun — expect FAILURE (the now-mismatched quadrant). Revert the
change, rebuild, confirm green again.

- [ ] **Step 4: Format check, commit**

```bash
clang-format --dry-run --Werror tests/test_gfx_texture.c
git add tests/test_gfx_texture.c
git commit -m "add a headless golden-image test for the textured indexed quad"
```

---

## Task 5: `bk_gfx` pending-slot extension — vertex/index/texture binding

**Files:**
- Modify: `include/bielik/bk_gfx.h`
- Modify: `src/internal/bk_gfx_internal.h`
- Modify: `src/bk_gfx.c`
- Modify: `tests/test_gfx.c`

- [ ] **Step 1: Write the failing test in `tests/test_gfx.c`**

Following the existing `test_bind_pipeline_and_draw_sets_pending_state` shape: fake
opaque pointers for `BK_GfxBuffer *`/`BK_GfxTexture *`/`BK_GfxSampler *` (never
dereferenced by the functions under test), call
`bk_gfx_bind_vertex_buffer`/`bk_gfx_bind_index_buffer`/`bk_gfx_bind_texture`/
`bk_gfx_draw_indexed(6)`, assert the new `bk__gfx_get_pending_*` accessors return what
was set.

- [ ] **Step 2: Verify it fails to build (undefined symbols)**

- [ ] **Step 3: Add public declarations to `include/bielik/bk_gfx.h`**

```c
void bk_gfx_bind_vertex_buffer(BK_GfxBuffer *buffer);
void bk_gfx_bind_index_buffer(BK_GfxBuffer *buffer);
void bk_gfx_bind_texture(BK_GfxTexture *texture, BK_GfxSampler *sampler);
void bk_gfx_draw_indexed(int index_count);
```

Add `#include <bielik/bk_gfx_buffer.h>` and `#include <bielik/bk_gfx_texture.h>`
alongside the existing `bk_gfx_pipeline.h` include.

- [ ] **Step 4: Add internal test accessors to `src/internal/bk_gfx_internal.h`**

`bk__gfx_get_pending_vertex_buffer`, `bk__gfx_get_pending_index_buffer`,
`bk__gfx_get_pending_texture`, `bk__gfx_get_pending_sampler`,
`bk__gfx_get_pending_index_count` — same test-only-accessor doc-comment style as the
existing pending-pipeline accessors.

- [ ] **Step 5: Implement in `src/bk_gfx.c`, integrate into `bk__gfx_flush`**

New static pending-state variables alongside the existing
`s_pending_pipeline`/`s_pending_vertex_count`. `bk__gfx_flush`'s existing
snapshot-then-clear block at the top grows to snapshot/clear these too. In the render
pass body, after the existing pipeline bind: if a vertex buffer is pending, bind it at
slot 0 (`SDL_BindGPUVertexBuffers`); if an index buffer is pending, bind it
(`SDL_BindGPUIndexBuffer`, `SDL_GPU_INDEXELEMENTSIZE_16BIT` hardcoded per spec §9); if
a texture+sampler pair is pending, bind as fragment sampler slot 0
(`SDL_BindGPUFragmentSamplers`); if `pending_index_count > 0`, call
`SDL_DrawGPUIndexedPrimitives` instead of (or in addition to — the two draw calls are
independent, a frame can do either or both) the existing `SDL_DrawGPUPrimitives` path.

- [ ] **Step 6: Run, verify green; full suite + format check; commit**

```bash
cmake --build build --target test_gfx && ./build/tests/test_gfx
cmake --build build && ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
git add include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
git commit -m "extend bk_gfx's pending slot with vertex/index buffer and texture binding"
```

---

## Task 6: `samples/04_textured_quad`

**Files:**
- Create: `samples/04_textured_quad/main.c`, `samples/04_textured_quad/CMakeLists.txt`
- Modify: `samples/CMakeLists.txt`

- [ ] **Step 1: Write `main.c`**

Follows `samples/03_triangle/main.c`'s shape (`BK_APP`/`BK_MAIN_HANDLED` twin targets,
`--frames N`). In `app_init`: build the `textured` pipeline (vertex layout as in Task
4), create+upload a vertex buffer (a full-quad NDC layout, e.g. centered, half-size)
and index buffer, procedurally generate an 8×8 (or similar) checkerboard pattern in C
(two alternating colors) sized as a small texture, create+upload it, create a `NEAREST`
sampler. In `app_render`: `bk_gfx_bind_pipeline`, `bk_gfx_bind_vertex_buffer`,
`bk_gfx_bind_index_buffer`, `bk_gfx_bind_texture`, `bk_gfx_draw_indexed(6)`. `app_quit`
destroys everything created in `init` (pipeline, buffers, texture, sampler).

- [ ] **Step 2: Write `samples/04_textured_quad/CMakeLists.txt`**

Mirror `samples/03_triangle/CMakeLists.txt` (both targets call `bk_stage_shaders`).

- [ ] **Step 3: Add to `samples/CMakeLists.txt`**

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build
./build/samples/04_textured_quad/04_textured_quad --frames 5; echo "exit: $?"
./build/samples/04_textured_quad/04_textured_quad_run --frames 5; echo "exit: $?"
```

If at a real desktop, drop `--frames 5` once and confirm visually: a checkerboard quad
renders, ESC/close quits cleanly.

- [ ] **Step 5: Format check, commit**

```bash
clang-format --dry-run --Werror samples/04_textured_quad/main.c
git add samples/04_textured_quad samples/CMakeLists.txt
git commit -m "add the 04_textured_quad sample"
```

**— 2a/2b checkpoint: pause here for a whole-slice review of buffers/textures before
starting compute. —**

---

## Task 7: Compute pipeline + dispatch

**Files:**
- Modify: `include/bielik/bk_gfx_pipeline.h` (compute types, spec §6)
- Modify: `src/bk_gfx_pipeline.c` (refactor `s_pick_shader_variant`/`s_create_shader`
  to be reusable; add compute create/destroy/dispatch)
- Create: `tests/test_gfx_compute.c`, `tests/test_header_bk_gfx_pipeline.c` already
  exists — no new header file, so no new stub needed
- Modify: `CMakeLists.txt` (no new `.c` file — `bk_gfx_pipeline.c` already linked),
  `tests/CMakeLists.txt`

- [ ] **Step 1: Refactor the shader-variant picker (no behavior change yet)**

Change `s_pick_shader_variant`'s signature to return `const BK_GfxShaderVariant *`
given `(SDL_GPUShaderFormat supported, const BK_GfxShaderVariant *spirv, *dxil, *msl)`
and a matched `SDL_GPUShaderFormat *out_format`, instead of the current four
out-parameters keyed off a `BK_GfxShaderDesc *`. Update `s_create_shader` to call it
with `&desc->spirv, &desc->dxil, &desc->msl`. Run `ctest` to confirm
`test_gfx_pipeline` still passes unchanged — this step is a pure refactor, verify it
doesn't silently change behavior before adding compute on top.

- [ ] **Step 2: Add compute types to `include/bielik/bk_gfx_pipeline.h`**

Copy `BK_GfxComputePipelineDesc`, `BK_GfxComputePipeline`,
`bk_gfx_compute_pipeline_create`/`destroy`, `BK_GfxComputeDispatchDesc`,
`bk_gfx_compute_dispatch` verbatim from spec §6. This header needs
`#include <bielik/bk_gfx_buffer.h>` and `#include <bielik/bk_gfx_texture.h>` added for
the dispatch desc's array types.

- [ ] **Step 3: Write the failing test `tests/test_gfx_compute.c`**

Headless device setup (matching Task 4's pattern). Load `gradient.compute.{spv,msl}`.
`bk_gfx_compute_pipeline_create` with `num_readonly_storage_buffers = 1`,
`num_readwrite_storage_textures = 1`, `threadcount_{x,y,z} = 8,8,1` matching the
shader's `local_size`. Create a small storage buffer (`STORAGE_READ` usage, 32 bytes —
two `vec4`s) and upload a hardcoded `base_color`/`scale` pair. Create a
`COMPUTE_TARGET` texture (e.g. 16×16 — small enough that 8×8 threadgroups need only a
2×2 dispatch grid, `groups_x = groups_y = ceil(16/8) = 2`, `groups_z = 1`).
`bk_gfx_compute_dispatch`, then `bk__gfx_download_texture` (needs its own command
buffer — the dispatch already submitted and fenced its own, so acquire a fresh one for
the download) and check pixels at a couple of known `uv` coordinates against
hand-computed expected values from the shader's `base_color + scale * uv` formula
(literal expected values, computed by hand from the hardcoded inputs — same
non-circular-assertion discipline as Task 4). Add
`bk_gfx_compute_pipeline_destroy(nullptr)` no-op coverage.

- [ ] **Step 4: Implement compute create/destroy/dispatch in `src/bk_gfx_pipeline.c`**

`BK_GfxComputePipeline` struct: `device`, `handle` (`SDL_GPUComputePipeline *`).
`bk_gfx_compute_pipeline_create` builds an `SDL_GPUComputePipelineCreateInfo` (code/
format/entrypoint from the refactored picker, `num_readonly_storage_buffers`,
`num_readwrite_storage_textures`, `threadcount_x/y/z`), calls
`SDL_CreateGPUComputePipeline`, wraps or logs+nullptr on failure — same shape as
`bk_gfx_pipeline_create`. `bk_gfx_compute_dispatch`: `BK_ASSERT(desc != nullptr &&
desc->pipeline != nullptr)`; acquire a command buffer
(`SDL_AcquireGPUCommandBuffer`), begin a compute pass
(`SDL_BeginGPUComputePass` with a `SDL_GPUStorageTextureReadWriteBinding` array built
from `readwrite_textures`), bind the pipeline (`SDL_BindGPUComputePipeline`), bind
storage buffers (`SDL_BindGPUComputeStorageBuffers`, from `readonly_buffers`'
`bk__gfx_buffer_handle`), dispatch (`SDL_DispatchGPUCompute`), end the compute pass,
submit-and-fence-wait (mirror `bk__gfx_download_texture`'s existing submit/fence/wait
block in `src/bk_gfx.c`), release the fence. Log+return `false` on any null result
along the way (`SDL_AcquireGPUCommandBuffer`, fence acquire, fence wait).

- [ ] **Step 5: Wire the test into `tests/CMakeLists.txt`, run, verify green**

```bash
cmake --build build --target test_gfx_compute && ./build/tests/test_gfx_compute
```

- [ ] **Step 6: Full suite + format check; commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx_pipeline.h src/bk_gfx_pipeline.c tests/test_gfx_compute.c
git add include/bielik/bk_gfx_pipeline.h src/bk_gfx_pipeline.c tests/test_gfx_compute.c tests/CMakeLists.txt
git commit -m "add compute pipeline creation and synchronous dispatch"
```

---

## Task 8: `samples/05_compute`

**Files:**
- Create: `samples/05_compute/main.c`, `samples/05_compute/CMakeLists.txt`
- Modify: `samples/CMakeLists.txt`

- [ ] **Step 1: Write `main.c`**

In `app_init`: build the `textured` graphics pipeline (reusing Task 6's vertex layout
and a full-viewport quad + index buffer, same as `04_textured_quad`), build the
`gradient` compute pipeline, create a `COMPUTE_TARGET` texture sized to something
visually clear (e.g. 256×256), create+upload a small storage buffer with a chosen
`base_color`/`scale`, call `bk_gfx_compute_dispatch` **once**, synchronously, right
there in `init` — not in `render` (per spec §6/§9: dispatch is setup-time, not
per-frame). Create a `LINEAR` sampler this time (contrast with `04`'s `NEAREST`, and
it's the right choice for a smooth gradient). In `app_render`: bind pipeline, vertex
buffer, index buffer, the compute-filled texture + sampler, `draw_indexed(6)` — same
every frame, no re-dispatch. `app_quit` destroys everything.

- [ ] **Step 2: Write `samples/05_compute/CMakeLists.txt`**, mirroring Task 6's.

- [ ] **Step 3: Add to `samples/CMakeLists.txt`**

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build
./build/samples/05_compute/05_compute --frames 5; echo "exit: $?"
./build/samples/05_compute/05_compute_run --frames 5; echo "exit: $?"
```

At a real desktop: confirm visually — a smooth gradient-filled quad, ESC/close quits.

- [ ] **Step 5: Format check, commit**

```bash
clang-format --dry-run --Werror samples/05_compute/main.c
git add samples/05_compute samples/CMakeLists.txt
git commit -m "add the 05_compute sample"
```

---

## Task 9: CI wiring, DEVIATIONS.md follow-through, whole-branch review, PR

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `DEVIATIONS.md`, `CLAUDE.md` (only if needed — see below)

- [ ] **Step 1: Add the three new GPU-dependent tests to CI**

`test_gfx_buffer`, `test_gfx_texture`, `test_gfx_compute` all create a real
`SDL_GPUDevice` — same allow-fail treatment as `test_app_lifecycle`/`test_gfx_pipeline`
per `PLAN.md` §6.10's start-conservative policy. In `.github/workflows/ci.yml`, extend
**all six** occurrences of the existing pattern (not four — this repo has grown a third
test group, `test_gfx_capture`, since sub-project 1's plan was written; check the
current file rather than assuming the old line numbers):

- The `-E "test_app_lifecycle|test_gfx_pipeline|test_gfx_capture"` exclusion → add
  `|test_gfx_buffer|test_gfx_texture|test_gfx_compute` (2 occurrences: `build-and-test`
  job's main `Test` step, `debug-sanitizers` job's `Test` step).
- The `-R "test_app_lifecycle|test_gfx_pipeline|test_gfx_capture"` inclusion → same
  addition (4 occurrences: Ubuntu + macOS/Windows GPU-dependent steps in both jobs).

- [ ] **Step 2: Confirm `DEVIATIONS.md` covers everything discovered this sub-project**

If Task 1's MSL gate needed remap flags, or Task 3's usage/format gate surfaced a
platform limitation requiring a design change, make sure each is recorded. If nothing
new surfaced beyond what was already committed in-task, this step is a no-op — confirm
by reading the diff, don't add filler entries.

- [ ] **Step 3: `CLAUDE.md` gotchas**

Only if Task 1/3's gates surfaced something non-obvious worth remembering for future
sessions (mirroring the existing "SDL_GPU gotchas" section) — e.g. a specific MSL
binding quirk, or a texture usage-flag combination that needed a workaround. Skip if
nothing qualifies; don't pad the file.

- [ ] **Step 4: Full clean build + test suite + format check across everything touched**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror $(git diff --name-only cb7c326... -- '*.c' '*.h')
```

- [ ] **Step 5: Commit CI/docs changes**

```bash
git add .github/workflows/ci.yml DEVIATIONS.md CLAUDE.md
git commit -m "add the new gfx-resources tests to CI's GPU-dependent group"
```

- [ ] **Step 6: Whole-branch review**

Read every commit's diff top to bottom against the spec's decisions (§9) — confirm no
public API deviations from the spec beyond what's recorded in `DEVIATIONS.md`, every
public symbol is doc-commented, `clang-format` is clean everywhere, and the two gates
(Task 1 MSL binding, Task 3 usage/format) both have a passing test backing them, not
just a manual check.

- [ ] **Step 7: Open the PR**

Push the branch, open a PR against `main` following the repo's PR description
conventions (why/non-obvious context, no changes-by-file restatement).

Optional, only if it's a small addition at this point: wire `bk_gfx_request_capture`
to `04_textured_quad`'s real rendered output to help close
`pusewicz/bielik2d-c#5` — not required scope for this sub-project, skip if it would
meaningfully delay the PR.
