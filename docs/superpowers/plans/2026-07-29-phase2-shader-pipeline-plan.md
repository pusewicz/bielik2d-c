# Phase 2 Sub-project 1: Shader Toolchain + Graphics Pipelines — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the `bk_gfx_pipeline` module (offline-compiled shaders + graphics pipeline
objects), a minimal bind/draw slot on `bk_gfx`, and a working `samples/03_triangle`
that draws a hardcoded triangle end to end.

**Architecture:** One new module (`bk_gfx_pipeline`) wraps SDL_GPU shader/pipeline
creation behind a device-format-agnostic descriptor. `bk_gfx` gains a single pending
bind+draw slot (not a draw list — PLAN.md reserves that for P3) that `bk__gfx_flush`
consults each frame. Shaders are authored in GLSL, compiled offline to SPIR-V (via
`glslc`) and cross-compiled to MSL (via `spirv-cross`), and the compiled bytecode is
committed to the repo — regeneration is find_program-gated and optional, not a
build-time requirement.

**Tech Stack:** C23, SDL3 GPU API, GLSL (`glslc`/`spirv-cross`), CMake, CTest.

## Global Constraints

- Spec of record: `docs/superpowers/specs/2026-07-29-phase2-shader-pipeline-design.md`
  (read it before starting — this plan implements it exactly, including its two
  amendment rounds).
- Naming: public functions `bk_` + snake_case, public types `BK_` + PascalCase, enum
  values `BK_` + UPPER_SNAKE, internal linker-visible symbols `bk__` prefix,
  file-static functions `s_` prefix (see `CLAUDE.md` Conventions).
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).
- `.clang-format`: LLVM base, 4-space indent, 100 columns, `PointerAlignment: Right`,
  K&R attached braces. Run `clang-format -i` on every file you touch before
  committing.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant.
- Errors: no silent failure. Recoverable runtime failures (bad shader bytecode,
  unsupported format) log via `SDL_Log` with a `"BK: "` prefix and return
  `nullptr`/fail — they do NOT assert. Programmer-error preconditions (null
  arguments, out-of-range inputs) use `BK_ASSERT`.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`.
  Test: `ctest --test-dir build --output-on-failure`.
- Commit style: atomic commits, human-voice messages (no Conventional Commits
  prefixes like `feat:`/`fix:`, no AI signoff/Co-Authored-By footers).
- `#embed` is reserved for later phases — do not use it.
- `nullptr`/`true`/`false`/`constexpr`/designated initializers/compound literals are
  all fair game (C23).

---

## Task 1: GLSL triangle shaders + compiled bytecode + shader CMake toolchain

**Files:**
- Create: `shaders/triangle.vert` (GLSL vertex source)
- Create: `shaders/triangle.frag` (GLSL fragment source)
- Create: `shaders/triangle.vertex.spv`, `shaders/triangle.vertex.msl`,
  `shaders/triangle.fragment.spv`, `shaders/triangle.fragment.msl` (compiled,
  committed bytecode)
- Create: `cmake/shaders.cmake`
- Modify: `CMakeLists.txt`
- Modify: `DEVIATIONS.md`

**Interfaces:**
- Produces: `shaders/triangle.{vertex,fragment}.{spv,msl}` on disk (consumed by Tasks
  2, 4, 5 via `SDL_LoadFile`). `bk_compile_shader(NAME <name> STAGE vertex|fragment)`
  and `bk_stage_shaders(<target>)` CMake functions (consumed by Tasks 2 and 5's
  CMakeLists.txt).

This task has no C code, so there's no red/green test cycle in the usual sense — the
"test" is that the tools produce valid, loadable bytecode, which Task 2 exercises for
real. Verify each step's output as you go rather than deferring all checking to the
end.

- [ ] **Step 1: Confirm `glslc` and `spirv-cross` are available**

Run: `which glslc && which spirv-cross`

If either is missing, install with `brew install shaderc` (provides `glslc`;
`spirv-cross` comes from the `spirv-cross` formula — install that too if missing:
`brew install spirv-cross`). Expected: both `which` commands print a path.

- [ ] **Step 2: Write the GLSL vertex shader**

Create `shaders/triangle.vert`:

```glsl
#version 450

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
```

- [ ] **Step 3: Write the GLSL fragment shader**

Create `shaders/triangle.frag`:

```glsl
#version 450

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(1.0, 0.0, 0.0, 1.0);
}
```

- [ ] **Step 4: Compile to SPIR-V and cross-compile to MSL, verify output**

Run, from the repo root:

```bash
glslc -fshader-stage=vertex shaders/triangle.vert -o shaders/triangle.vertex.spv
glslc -fshader-stage=fragment shaders/triangle.frag -o shaders/triangle.fragment.spv
spirv-cross --msl shaders/triangle.vertex.spv --output shaders/triangle.vertex.msl
spirv-cross --msl shaders/triangle.fragment.spv --output shaders/triangle.fragment.msl
```

Expected: all four commands exit 0, and `shaders/triangle.{vertex,fragment}.{spv,msl}`
exist with non-zero size (`ls -la shaders/`). Open `shaders/triangle.vertex.msl` and
confirm it contains a function literally named `main0` (spirv-cross renames `main` to
`main0` in MSL output — this exact name is required in Task 2/4/5's
`BK_GfxShaderVariant.entry_point` for the MSL variant). Confirm
`shaders/triangle.vertex.spv`'s SPIR-V entry point is `main` (the GLSL source's
unchanged name) via `spirv-cross shaders/triangle.vertex.spv --reflect | grep '"name"'`
— expect `"name" : "main"`.

- [ ] **Step 5: Write `cmake/shaders.cmake`**

Create `cmake/shaders.cmake`:

```cmake
find_program(BK_GLSLC_EXE glslc)
find_program(BK_SPIRV_CROSS_EXE spirv-cross)

# Regenerates shaders/<name>.<stage>.spv and .msl from shaders/<name>.<vert|frag> via
# glslc + spirv-cross. The regenerated files are committed to the repo, not produced
# fresh by every build -- this is a no-op (just logs) if the tools aren't installed,
# so shader authoring stays an occasional opt-in step, not a build-time requirement.
function(bk_compile_shader)
    set(one_value_args NAME STAGE)
    cmake_parse_arguments(ARG "" "${one_value_args}" "" ${ARGN})

    if(NOT BK_GLSLC_EXE OR NOT BK_SPIRV_CROSS_EXE)
        message(STATUS "glslc/spirv-cross not found -- using committed shader bytecode for ${ARG_NAME}.${ARG_STAGE}")
        return()
    endif()

    if(ARG_STAGE STREQUAL "vertex")
        set(glslc_stage "vertex")
        set(src_ext "vert")
    elseif(ARG_STAGE STREQUAL "fragment")
        set(glslc_stage "fragment")
        set(src_ext "frag")
    else()
        message(FATAL_ERROR "bk_compile_shader: unknown STAGE '${ARG_STAGE}' (expected vertex or fragment)")
    endif()

    set(src "${CMAKE_SOURCE_DIR}/shaders/${ARG_NAME}.${src_ext}")
    set(spv "${CMAKE_SOURCE_DIR}/shaders/${ARG_NAME}.${ARG_STAGE}.spv")
    set(msl "${CMAKE_SOURCE_DIR}/shaders/${ARG_NAME}.${ARG_STAGE}.msl")

    add_custom_command(
        OUTPUT "${spv}"
        COMMAND "${BK_GLSLC_EXE}" -fshader-stage=${glslc_stage} "${src}" -o "${spv}"
        DEPENDS "${src}"
        COMMENT "glslc: ${ARG_NAME}.${src_ext} -> ${ARG_NAME}.${ARG_STAGE}.spv"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${msl}"
        COMMAND "${BK_SPIRV_CROSS_EXE}" --msl "${spv}" --output "${msl}"
        DEPENDS "${spv}"
        COMMENT "spirv-cross: ${ARG_NAME}.${ARG_STAGE}.spv -> ${ARG_NAME}.${ARG_STAGE}.msl"
        VERBATIM
    )
    add_custom_target(bk_shader_${ARG_NAME}_${ARG_STAGE} DEPENDS "${spv}" "${msl}")
endfunction()

# Copies shaders/ next to TARGET's built binary (POST_BUILD) so it can load shader
# bytecode with a path relative to its own executable (via SDL_GetBasePath) no
# matter what working directory it's run from.
function(bk_stage_shaders TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMENT "Staging shaders/ next to ${TARGET}"
    )
endfunction()
```

- [ ] **Step 6: Wire `cmake/shaders.cmake` into the top-level build**

In `CMakeLists.txt`, after `include(cmake/warnings.cmake)` (line 15), add:

```cmake
include(cmake/shaders.cmake)
```

After the `add_library(bielik ...)` block (after the existing
`target_link_libraries(bielik PRIVATE bk_warnings)` line, before
`if(BK_BUILD_TESTS)`), add:

```cmake
bk_compile_shader(NAME triangle STAGE vertex)
bk_compile_shader(NAME triangle STAGE fragment)
```

- [ ] **Step 7: Verify the optional regeneration path works**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build --target bk_shader_triangle_vertex
cmake --build build --target bk_shader_triangle_fragment
git status shaders/
```

Expected: both custom targets build successfully (glslc/spirv-cross re-run), and
`git status shaders/` shows no changes (regeneration is deterministic and matches
what's already committed from Step 4). If `git status` shows differences, something
about the regenerated output differs from Step 4's — investigate before proceeding
(likely a stale file from a partial Step 4 run).

- [ ] **Step 8: Record the toolchain deviation in `DEVIATIONS.md`**

Add this entry to `DEVIATIONS.md` (append after the existing entries, keeping the
file's `## <title> (<source>)` + paragraph format):

```markdown
## Shader toolchain substitutes glslc+spirv-cross/GLSL for shadercross+HLSL (docs/superpowers/specs/2026-07-29-phase2-shader-pipeline-design.md §2)

`CLAUDE.md`'s locked shader decision names SDL_shadercross (HLSL/SPIR-V ->
SPIR-V/DXIL/MSL) as the offline shader toolchain. Neither `shadercross` nor
DirectXShaderCompiler (DXC) is installed anywhere this sub-project was
implemented, and DXC has no prebuilt macOS binary anywhere in
SDL_shadercross's own tooling -- the only supported macOS path builds a
vendored LLVM/Clang fork from source, which is impractical to do as part of
implementing one shader pair. `glslc` (from Google's `shaderc`) and
`spirv-cross` are real, Homebrew-installable tools with no DXC dependency,
verified end to end (GLSL -> SPIR-V via `glslc`, SPIR-V -> MSL via
`spirv-cross`) before committing to this plan. `shaders/triangle.vert` and
`shaders/triangle.frag` are therefore authored in GLSL, not HLSL, and their
committed bytecode covers SPIR-V and MSL only -- no DXIL variant exists,
since no available tool can produce it without DXC.
`bk_gfx_pipeline_create` fails gracefully (logs, returns `nullptr`) on any
device that only supports DXIL (Windows/D3D12) until someone with real DXC
tooling generates the DXIL variant; CI's GPU-dependent tests are already
`continue-on-error: true` on every platform, so this doesn't block required
CI. Migrating to the shadercross/HLSL toolchain is future work once that
tooling is genuinely available, not a reversal of the locked decision.
```

- [ ] **Step 9: Commit**

```bash
git add shaders/ cmake/shaders.cmake CMakeLists.txt DEVIATIONS.md
git commit -m "add the GLSL triangle shaders and the shader compile toolchain"
```

---

## Task 2: `bk_gfx_pipeline` module — shader/pipeline descriptors, create/destroy

**Files:**
- Create: `include/bielik/bk_gfx_pipeline.h`
- Create: `src/bk_gfx_pipeline.c`
- Create: `src/internal/bk_gfx_pipeline_internal.h`
- Create: `tests/test_gfx_pipeline.c`
- Create: `tests/test_header_bk_gfx_pipeline.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `shaders/triangle.{vertex,fragment}.{spv,msl}` (Task 1). `bk__alloc`/
  `bk__free` from `src/internal/bk_app_internal.h` (existing, Phase 1).
  `BK_ASSERT` from `<bielik/bk_app.h>` (existing, Phase 1).
- Produces: `BK_GfxShaderVariant`, `BK_GfxShaderDesc`, `BK_GfxVertexFormat`,
  `BK_GfxVertexAttribute`, `BK_GfxVertexBufferLayout`, `BK_GfxPrimitiveType`,
  `BK_GfxBlendMode`, `BK_GfxPipeline` (opaque), `BK_GfxPipelineDesc`,
  `BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc)`,
  `void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline)` — all consumed by Task 3
  (frame integration), Task 4 (golden-image test), Task 5 (sample).
  `SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline)`
  (internal) — consumed by Task 3's `bk__gfx_flush` and Task 4's test.

- [ ] **Step 1: Write the public header `include/bielik/bk_gfx_pipeline.h`**

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
/// declares (SDL_GPU validates this at creation). A variant with code == nullptr is
/// treated as unavailable.
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

    // nullptr/0 => no vertex input (e.g. a procedural triangle driven by
    // gl_VertexIndex/SV_VertexID with no bound vertex buffer). Max 8 buffers, 16
    // attributes.
    const BK_GfxVertexBufferLayout *vertex_buffers;
    int num_vertex_buffers;
    const BK_GfxVertexAttribute *vertex_attributes;
    int num_vertex_attributes;

    BK_GfxPrimitiveType primitive_type;

    // Caller supplies the target format explicitly -- SDL_GetGPUSwapchainTextureFormat
    // for on-screen rendering; an offscreen texture's own format for headless/canvas
    // use. No bk_ wrapper needed.
    SDL_GPUTextureFormat color_target_format;
    BK_GfxBlendMode blend_mode;
} BK_GfxPipelineDesc;

/// Creates a graphics pipeline against the given device. Logs via SDL_Log ("BK: "
/// prefix) and returns nullptr on any SDL_GPU failure (bad bytecode, unsupported
/// format/resource combination) -- this is a runtime-data-dependent operation, not a
/// programmer-error precondition, so failure is a recoverable return, not an assert.
/// device is explicit (not the bk_gpu() singleton) so pipelines can be created and
/// tested without a running app or window.
BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc);

/// Destroys a pipeline. No-op if pipeline is nullptr.
void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline);
```

- [ ] **Step 2: Write the internal header `src/internal/bk_gfx_pipeline_internal.h`**

```c
#pragma once
#include <SDL3/SDL_gpu.h>
#include <bielik/bk_gfx_pipeline.h>

/// Returns the underlying SDL_GPU pipeline handle. Framework-internal; used by
/// bk_gfx's frame flush to bind the pipeline pending from bk_gfx_bind_pipeline
/// (added in a later task).
SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline);
```

- [ ] **Step 3: Write the failing test `tests/test_gfx_pipeline.c`**

```c
#include "bk_test.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_pipeline.h>
#include <stdio.h>

static void *s_load_shader_file(const char *relative_path, size_t *out_size) {
    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", SDL_GetBasePath(), relative_path);
    void *data = SDL_LoadFile(path, out_size);
    REQUIRE(data != nullptr);
    return data;
}

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

static void test_create_and_destroy_pipeline_succeeds(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };

    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &desc);
    REQUIRE(pipeline != nullptr);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_pipeline_destroy(nullptr); }

int main(void) {
    test_create_and_destroy_pipeline_succeeds();
    test_destroy_null_is_noop();
    printf("test_gfx_pipeline: OK\n");
    return 0;
}
```

Also create `tests/test_header_bk_gfx_pipeline.c` (standalone-compile check, matching
the existing `test_header_bk_*.c` stubs):

```c
#include <bielik/bk_gfx_pipeline.h>
```

- [ ] **Step 4: Wire the test into `tests/CMakeLists.txt` and verify it fails to build**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_gfx_pipeline test_gfx_pipeline.c)
target_link_libraries(test_gfx_pipeline PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_pipeline PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_pipeline COMMAND test_gfx_pipeline)
bk_stage_shaders(test_gfx_pipeline)

add_library(test_header_bk_gfx_pipeline OBJECT test_header_bk_gfx_pipeline.c)
target_link_libraries(test_header_bk_gfx_pipeline PRIVATE bielik bk_warnings)
```

Add `src/bk_gfx_pipeline.c` to the library sources in the top-level `CMakeLists.txt`
(the `add_library(bielik STATIC ...)` line): change it to

```cmake
add_library(bielik STATIC src/bk_app.c src/bk_gfx.c src/bk_gfx_pipeline.c src/bk_task.c src/bk_time.c)
```

Create an empty stub `src/bk_gfx_pipeline.c` containing just
`#include "internal/bk_gfx_pipeline_internal.h"` so the build fails at the *link*
step (missing `bk_gfx_pipeline_create`/`bk_gfx_pipeline_destroy`/
`bk__gfx_pipeline_handle` symbols) rather than the configure step.

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build --target test_gfx_pipeline`

Expected: FAIL with undefined-symbol linker errors for `bk_gfx_pipeline_create` and
`bk_gfx_pipeline_destroy`.

- [ ] **Step 5: Implement `src/bk_gfx_pipeline.c`**

Replace the stub with:

```c
#include "internal/bk_gfx_pipeline_internal.h"
#include "internal/bk_app_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx_pipeline.h>

struct BK_GfxPipeline {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *handle;
};

static bool s_pick_shader_variant(SDL_GPUShaderFormat supported, const BK_GfxShaderDesc *desc,
                                   const void **out_code, size_t *out_code_size,
                                   const char **out_entry_point, SDL_GPUShaderFormat *out_format) {
    if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) && desc->spirv.code != nullptr) {
        *out_code = desc->spirv.code;
        *out_code_size = desc->spirv.code_size;
        *out_entry_point = desc->spirv.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_SPIRV;
        return true;
    }
    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) && desc->dxil.code != nullptr) {
        *out_code = desc->dxil.code;
        *out_code_size = desc->dxil.code_size;
        *out_entry_point = desc->dxil.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_DXIL;
        return true;
    }
    if ((supported & SDL_GPU_SHADERFORMAT_MSL) && desc->msl.code != nullptr) {
        *out_code = desc->msl.code;
        *out_code_size = desc->msl.code_size;
        *out_entry_point = desc->msl.entry_point;
        *out_format = SDL_GPU_SHADERFORMAT_MSL;
        return true;
    }
    return false;
}

static SDL_GPUShader *s_create_shader(SDL_GPUDevice *device, const BK_GfxShaderDesc *desc,
                                       SDL_GPUShaderStage stage) {
    const void *code = nullptr;
    size_t code_size = 0;
    const char *entry_point = nullptr;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;

    if (!s_pick_shader_variant(SDL_GetGPUShaderFormats(device), desc, &code, &code_size,
                                &entry_point, &format)) {
        SDL_Log("BK: bk_gfx_pipeline_create: no shader variant matches the device's supported "
                "formats");
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {
        .code_size = code_size,
        .code = code,
        .entrypoint = entry_point,
        .format = format,
        .stage = stage,
        .num_samplers = (Uint32)desc->num_samplers,
        .num_uniform_buffers = (Uint32)desc->num_uniform_buffers,
    };
    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (shader == nullptr) {
        SDL_Log("BK: SDL_CreateGPUShader failed: %s", SDL_GetError());
    }
    return shader;
}

static SDL_GPUVertexElementFormat s_vertex_format(BK_GfxVertexFormat format) {
    switch (format) {
    case BK_GFX_VERTEX_FORMAT_FLOAT2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case BK_GFX_VERTEX_FORMAT_FLOAT3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case BK_GFX_VERTEX_FORMAT_FLOAT4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case BK_GFX_VERTEX_FORMAT_UBYTE4_NORM:
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    BK_ASSERT(false);
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

static SDL_GPUPrimitiveType s_primitive_type(BK_GfxPrimitiveType type) {
    switch (type) {
    case BK_GFX_PRIMITIVE_TRIANGLE_LIST:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    case BK_GFX_PRIMITIVE_TRIANGLE_STRIP:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    case BK_GFX_PRIMITIVE_LINE_LIST:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    }
    BK_ASSERT(false);
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

BK_GfxPipeline *bk_gfx_pipeline_create(SDL_GPUDevice *device, const BK_GfxPipelineDesc *desc) {
    BK_ASSERT(device != nullptr);
    BK_ASSERT(desc != nullptr);
    BK_ASSERT(desc->num_vertex_buffers <= 8);
    BK_ASSERT(desc->num_vertex_attributes <= 16);

    SDL_GPUShader *vertex_shader =
        s_create_shader(device, &desc->vertex_shader, SDL_GPU_SHADERSTAGE_VERTEX);
    if (vertex_shader == nullptr) {
        return nullptr;
    }
    SDL_GPUShader *fragment_shader =
        s_create_shader(device, &desc->fragment_shader, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (fragment_shader == nullptr) {
        SDL_ReleaseGPUShader(device, vertex_shader);
        return nullptr;
    }

    SDL_GPUVertexBufferDescription sdl_buffers[8];
    for (int i = 0; i < desc->num_vertex_buffers; i++) {
        sdl_buffers[i] = (SDL_GPUVertexBufferDescription){
            .slot = desc->vertex_buffers[i].slot,
            .pitch = desc->vertex_buffers[i].pitch,
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        };
    }

    SDL_GPUVertexAttribute sdl_attrs[16];
    for (int i = 0; i < desc->num_vertex_attributes; i++) {
        sdl_attrs[i] = (SDL_GPUVertexAttribute){
            .location = desc->vertex_attributes[i].location,
            .buffer_slot = desc->vertex_attributes[i].buffer_slot,
            .format = s_vertex_format(desc->vertex_attributes[i].format),
            .offset = desc->vertex_attributes[i].offset,
        };
    }

    SDL_GPUColorTargetBlendState blend = {0};
    if (desc->blend_mode == BK_GFX_BLEND_ALPHA) {
        blend = (SDL_GPUColorTargetBlendState){
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        };
    }

    SDL_GPUColorTargetDescription color_target = {
        .format = desc->color_target_format,
        .blend_state = blend,
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = sdl_buffers,
                .num_vertex_buffers = (Uint32)desc->num_vertex_buffers,
                .vertex_attributes = sdl_attrs,
                .num_vertex_attributes = (Uint32)desc->num_vertex_attributes,
            },
        .primitive_type = s_primitive_type(desc->primitive_type),
        .target_info =
            {
                .color_target_descriptions = &color_target,
                .num_color_targets = 1,
            },
    };

    SDL_GPUGraphicsPipeline *handle = SDL_CreateGPUGraphicsPipeline(device, &info);

    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    if (handle == nullptr) {
        SDL_Log("BK: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return nullptr;
    }

    BK_GfxPipeline *pipeline = bk__alloc(sizeof(BK_GfxPipeline));
    if (pipeline == nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device, handle);
        return nullptr;
    }
    pipeline->device = device;
    pipeline->handle = handle;
    return pipeline;
}

void bk_gfx_pipeline_destroy(BK_GfxPipeline *pipeline) {
    if (pipeline == nullptr) {
        return;
    }
    SDL_ReleaseGPUGraphicsPipeline(pipeline->device, pipeline->handle);
    bk__free(pipeline);
}

SDL_GPUGraphicsPipeline *bk__gfx_pipeline_handle(const BK_GfxPipeline *pipeline) {
    BK_ASSERT(pipeline != nullptr);
    return pipeline->handle;
}
```

- [ ] **Step 6: Run the test and verify it passes**

Run: `cmake --build build --target test_gfx_pipeline && ./build/tests/test_gfx_pipeline`

Expected: prints `test_gfx_pipeline: OK` and exits 0.

- [ ] **Step 7: Add the new test to CI's GPU-dependent (allow-fail) group**

This test creates a real `SDL_GPUDevice` (no window, but still needs a working
GPU/Vulkan/Metal backend), so it needs the same `continue-on-error` treatment
`test_app_lifecycle` already gets in `.github/workflows/ci.yml` — not because it's
expected to fail, but because Phase 1 established this as the deliberate
start-conservative policy for any GPU-dependent test (see `PLAN.md` 6.10: run
allow-fail first, promote to required only after proving stable across runs).

In `.github/workflows/ci.yml`, change all four occurrences of the pattern below (two
in the `build-and-test` job, one of them the main required step and two the
GPU-dependent steps; one in the `debug-sanitizers` job):

- `-E test_app_lifecycle` → `-E "test_app_lifecycle|test_gfx_pipeline"` (2
  occurrences: line 41's main `Test` step, and line 87's `debug-sanitizers` `Test`
  step)
- `-R test_app_lifecycle` → `-R "test_app_lifecycle|test_gfx_pipeline"` (2
  occurrences: line 46's Ubuntu GPU-dependent step, line 51's macOS/Windows
  GPU-dependent step)

- [ ] **Step 8: Full clean build + test suite, format check**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx_pipeline.h src/bk_gfx_pipeline.c src/internal/bk_gfx_pipeline_internal.h tests/test_gfx_pipeline.c tests/test_header_bk_gfx_pipeline.c
```

Expected: build succeeds, all tests pass (including the new `test_gfx_pipeline` and
`test_header_bk_gfx_pipeline`), clang-format reports no diffs.

- [ ] **Step 9: Commit**

```bash
git add include/bielik/bk_gfx_pipeline.h src/bk_gfx_pipeline.c src/internal/bk_gfx_pipeline_internal.h tests/test_gfx_pipeline.c tests/test_header_bk_gfx_pipeline.c CMakeLists.txt tests/CMakeLists.txt .github/workflows/ci.yml
git commit -m "add the bk_gfx_pipeline module"
```

---

## Task 3: `bk_gfx` bind+draw pending-state slot + frame flush integration

**Files:**
- Modify: `include/bielik/bk_gfx.h`
- Modify: `src/internal/bk_gfx_internal.h`
- Modify: `src/bk_gfx.c`
- Modify: `tests/test_gfx.c`

**Interfaces:**
- Consumes: `BK_GfxPipeline`, `bk__gfx_pipeline_handle` (Task 2).
- Produces: `void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline)`,
  `void bk_gfx_draw(int vertex_count)` — consumed by Task 5's sample.
  `BK_GfxPipeline *bk__gfx_get_pending_pipeline(void)`,
  `int bk__gfx_get_pending_vertex_count(void)` (internal, test-only).

- [ ] **Step 1: Write the failing test in `tests/test_gfx.c`**

Add this function to `tests/test_gfx.c` (after the existing `test_last_set_wins`
function):

```c
static void test_bind_pipeline_and_draw_sets_pending_state(void) {
    int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);

    REQUIRE(bk__gfx_get_pending_pipeline() == fake_pipeline);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 3);
}
```

Add a call to it in `main()`, right after `test_last_set_wins();`:

```c
    test_bind_pipeline_and_draw_sets_pending_state();
```

(`fake_pipeline` is never dereferenced by the functions under test — they only
store the pointer — so a stack address cast through the opaque `BK_GfxPipeline *`
type is a valid, safe way to test the state-storage logic without a real GPU
device.)

- [ ] **Step 2: Run the test and verify it fails to build**

Run: `cmake --build build --target test_gfx`

Expected: FAIL with "unknown type name 'BK_GfxPipeline'" and/or undefined-reference
errors for `bk_gfx_bind_pipeline`, `bk_gfx_draw`, `bk__gfx_get_pending_pipeline`,
`bk__gfx_get_pending_vertex_count`.

- [ ] **Step 3: Add the public declarations to `include/bielik/bk_gfx.h`**

Add `#include <bielik/bk_gfx_pipeline.h>` after the existing `#pragma once` line, then
append after `bk_gfx_set_clear_color`'s declaration:

```c
/// Binds a pipeline to be used by the next bk_gfx_draw call this frame. The
/// binding is consumed (cleared) by the frame's flush.
void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline);

/// Issues a draw of vertex_count vertices using the most recently bound pipeline.
/// Must be called after bk_gfx_bind_pipeline in the same frame.
void bk_gfx_draw(int vertex_count);
```

- [ ] **Step 4: Add the internal test accessors to `src/internal/bk_gfx_internal.h`**

Add `#include <bielik/bk_gfx_pipeline.h>` after the existing `#include
<bielik/bk_gfx.h>` line, then append:

```c
/// Test-only accessor: returns the pipeline bound via bk_gfx_bind_pipeline this
/// frame, or nullptr if none has been bound since the last flush.
BK_GfxPipeline *bk__gfx_get_pending_pipeline(void);

/// Test-only accessor: returns the vertex count set via bk_gfx_draw this frame, or
/// 0 if bk_gfx_draw hasn't been called since the last flush.
int bk__gfx_get_pending_vertex_count(void);
```

- [ ] **Step 5: Implement in `src/bk_gfx.c` and integrate into `bk__gfx_flush`**

Add `#include "internal/bk_gfx_pipeline_internal.h"` to the top of `src/bk_gfx.c`
(alongside the existing includes).

Add, after `bk__gfx_get_clear_color`'s definition:

```c
static BK_GfxPipeline *s_pending_pipeline = nullptr;
static int s_pending_vertex_count = 0;

void bk_gfx_bind_pipeline(BK_GfxPipeline *pipeline) {
    BK_ASSERT(pipeline != nullptr);
    s_pending_pipeline = pipeline;
}

void bk_gfx_draw(int vertex_count) {
    BK_ASSERT(vertex_count > 0);
    s_pending_vertex_count = vertex_count;
}

BK_GfxPipeline *bk__gfx_get_pending_pipeline(void) { return s_pending_pipeline; }

int bk__gfx_get_pending_vertex_count(void) { return s_pending_vertex_count; }
```

In `bk__gfx_flush`, change:

```c
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    SDL_EndGPURenderPass(pass);
```

to:

```c
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (s_pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(s_pending_pipeline));
        SDL_DrawGPUPrimitives(pass, (Uint32)s_pending_vertex_count, 1, 0, 0);
        s_pending_pipeline = nullptr;
        s_pending_vertex_count = 0;
    }
    SDL_EndGPURenderPass(pass);
```

- [ ] **Step 6: Run the test and verify it passes**

Run: `cmake --build build --target test_gfx && ./build/tests/test_gfx`

Expected: prints `test_gfx: OK` and exits 0.

- [ ] **Step 7: Full test suite + format check**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
```

Expected: all tests pass, no format diffs.

- [ ] **Step 8: Commit**

```bash
git add include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
git commit -m "give bk_gfx a single pending bind+draw slot"
```

---

## Task 4: Golden-image test — headless draw verification

**Files:**
- Modify: `tests/test_gfx_pipeline.c`

**Interfaces:**
- Consumes: everything from Task 2 (`bk_gfx_pipeline_create`/`destroy`,
  `bk__gfx_pipeline_handle`) plus raw SDL_GPU calls directly (this test does not go
  through `bk_run`/`BK_AppDesc` — it drives its own headless device, matching the
  spec's explicit "no window or swapchain needed" design).

This task proves the whole chain (shader bytecode -> pipeline -> bind -> draw)
actually produces correct pixels, not just that the objects construct without
error.

- [ ] **Step 1: Add the golden-image test to `tests/test_gfx_pipeline.c`**

Add `#include <stdlib.h>` to the top of the file (needed for `abs()`).

Add this function before `main()`:

```c
static void s_check_pixel(const uint8_t *pixels, int width, int x, int y, uint8_t r, uint8_t g,
                           uint8_t b, uint8_t a, int tolerance) {
    size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4;
    REQUIRE(abs((int)pixels[i + 0] - (int)r) <= tolerance);
    REQUIRE(abs((int)pixels[i + 1] - (int)g) <= tolerance);
    REQUIRE(abs((int)pixels[i + 2] - (int)b) <= tolerance);
    REQUIRE(abs((int)pixels[i + 3] - (int)a) <= tolerance);
}

static void test_draw_produces_expected_pixels(void) {
    constexpr int size = 64;
    constexpr int tolerance = 5;

    // Defensive, independent of test-function call order: see the identical call
    // in test_create_and_destroy_pipeline_succeeds above for why this is required.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc pipeline_desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &pipeline_desc);
    REQUIRE(pipeline != nullptr);

    SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = size,
        .height = size,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    SDL_GPUTexture *offscreen = SDL_CreateGPUTexture(device, &texture_info);
    REQUIRE(offscreen != nullptr);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = (Uint32)(size * size * 4),
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    REQUIRE(transfer != nullptr);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    REQUIRE(cmd != nullptr);

    SDL_GPUColorTargetInfo color_target = {
        .texture = offscreen,
        .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pipeline));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {
        .texture = offscreen, .x = 0, .y = 0, .z = 0, .w = size, .h = size, .d = 1};
    SDL_GPUTextureTransferInfo dst = {
        .transfer_buffer = transfer, .offset = 0, .pixels_per_row = size, .rows_per_layer = size};
    SDL_DownloadFromGPUTexture(copy_pass, &src, &dst);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    REQUIRE(fence != nullptr);
    REQUIRE(SDL_WaitForGPUFences(device, true, &fence, 1));
    SDL_ReleaseGPUFence(device, fence);

    const uint8_t *pixels = SDL_MapGPUTransferBuffer(device, transfer, false);
    REQUIRE(pixels != nullptr);

    // Center: well inside the triangle (NDC bbox [-0.5,0.5] on both axes covers the
    // middle half of the viewport) -> solid red.
    s_check_pixel(pixels, size, size / 2, size / 2, 255, 0, 0, 255, tolerance);
    // Corners, inset by 1px: outside the triangle's bounding box under any backend's
    // NDC-to-pixel axis convention -> clear color (black).
    s_check_pixel(pixels, size, 1, 1, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, size - 2, 1, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, 1, size - 2, 0, 0, 0, 255, tolerance);
    s_check_pixel(pixels, size, size - 2, size - 2, 0, 0, 0, 255, tolerance);

    SDL_UnmapGPUTransferBuffer(device, transfer);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, offscreen);
    SDL_DestroyGPUDevice(device);
}
```

Add a call to it in `main()`, between `test_create_and_destroy_pipeline_succeeds();`
and `test_destroy_null_is_noop();`:

```c
    test_draw_produces_expected_pixels();
```

- [ ] **Step 2: Run the test and verify it exercises real rendering**

Run: `cmake --build build --target test_gfx_pipeline && ./build/tests/test_gfx_pipeline`

Expected: prints `test_gfx_pipeline: OK` and exits 0. If any `s_check_pixel` call
fails, the failure message (from `bk_test.h`'s `REQUIRE`) prints the file:line and
the failing comparison expression — use it to diagnose (common causes: wrong
`pixels_per_row`/stride, wrong clear color, an inverted Y assumption baked into which
corner you're checking, though the corner choice here is deliberately
flip-agnostic).

- [ ] **Step 3: Mutation-test the tolerance check**

Temporarily change the fragment shader's output color in `shaders/triangle.frag` from
`vec4(1.0, 0.0, 0.0, 1.0)` to `vec4(0.0, 1.0, 0.0, 1.0)` (green), regenerate
(`cmake --build build --target bk_shader_triangle_fragment`), rebuild and rerun the
test — expect it to now FAIL (center pixel no longer matches the hardcoded red
assertion), proving the test actually discriminates. Then revert:
`git checkout shaders/triangle.frag` and regenerate again
(`cmake --build build --target bk_shader_triangle_fragment`) to restore the
original committed bytecode. Run `git status shaders/` to confirm everything matches
Task 1's committed state.

- [ ] **Step 4: Full test suite + format check**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror tests/test_gfx_pipeline.c
```

Expected: all tests pass, no format diffs.

- [ ] **Step 5: Commit**

```bash
git add tests/test_gfx_pipeline.c
git commit -m "add a headless golden-image test for bk_gfx_pipeline"
```

---

## Task 5: `samples/03_triangle`

**Files:**
- Create: `samples/03_triangle/main.c`
- Create: `samples/03_triangle/CMakeLists.txt`
- Modify: `samples/CMakeLists.txt`

**Interfaces:**
- Consumes: `bk_gfx_pipeline_create`/`destroy` (Task 2), `bk_gfx_bind_pipeline`/
  `bk_gfx_draw` (Task 3), `shaders/triangle.{vertex,fragment}.{spv,msl}` (Task 1).

No CTest coverage (matches `01_clear`/`02_ticks` precedent — samples are
documentation-as-code, verified by a smoke run with `--frames N`, not an automated
test).

- [ ] **Step 1: Write `samples/03_triangle/main.c`**

```c
// 03_triangle — the smallest possible use of bk_gfx_pipeline: load the precompiled
// shader bytecode produced offline (see shaders/triangle.{vert,frag} and
// cmake/shaders.cmake), build a pipeline, and draw a hardcoded triangle with no
// vertex buffer (the vertex shader generates positions from gl_VertexIndex). Built
// once, using the BK_APP entry-point macro like 01_clear.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_main.h>

#include <stdlib.h>
#include <string.h>

typedef struct AppState {
    BK_GfxPipeline *pipeline;
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, size_t *out_size) {
    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", SDL_GetBasePath(), relative_path);
    return SDL_LoadFile(path, out_size);
}

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

    BK_GfxShaderDesc desc = {0};
    desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
    desc.spirv.entry_point = "main";
    desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
    desc.msl.entry_point = "main0";
    return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
    SDL_free((void *)desc->spirv.code);
    SDL_free((void *)desc->msl.code);
}

// init: create the pipeline here, once, up front -- the framework has already
// created the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);

    s_free_shader(&vertex);
    s_free_shader(&fragment);

    if (s_state.pipeline == nullptr) {
        return BK_FAIL;
    }

    *state = &s_state;
    return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as 01_clear/02_ticks.
static BK_Result app_update(void *state, const BK_FrameInfo *f) {
    (void)f;
    AppState *s = state;
    s->frame_count++;
    if (s->frame_limit > 0 && s->frame_count >= s->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: bind the pipeline and draw 3 vertices. The framework's frame pipeline
// calls bk__gfx_flush (clear + bind/draw + present) right after render returns.
static void app_render(void *state, const BK_FrameInfo *f) {
    (void)f;
    AppState *s = state;
    bk_gfx_bind_pipeline(s->pipeline);
    bk_gfx_draw(3);
}

static BK_Result app_event(void *state, const SDL_Event *e) {
    (void)state;
    if (e->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
    (void)result;
    AppState *s = state;
    bk_gfx_pipeline_destroy(s->pipeline);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
        .quit = app_quit,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
```

- [ ] **Step 2: Write `samples/03_triangle/CMakeLists.txt`**

```cmake
add_executable(03_triangle main.c)
target_link_libraries(03_triangle PRIVATE bielik bk_warnings)
bk_stage_shaders(03_triangle)

add_executable(03_triangle_run main.c)
target_link_libraries(03_triangle_run PRIVATE bielik bk_warnings)
target_compile_definitions(03_triangle_run PRIVATE BK_MAIN_HANDLED)
bk_stage_shaders(03_triangle_run)
```

- [ ] **Step 3: Add the subdirectory to `samples/CMakeLists.txt`**

Add `add_subdirectory(03_triangle)` after the existing `add_subdirectory(02_ticks)`
line.

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build
./build/samples/03_triangle/03_triangle --frames 5
echo "exit: $?"
./build/samples/03_triangle/03_triangle_run --frames 5
echo "exit: $?"
```

Expected: both exit 0. If you're at a real desktop (not headless CI), drop
`--frames 5` from one run and confirm visually: a window opens showing a red
triangle on a dark background, ESC and the close button both quit cleanly.

- [ ] **Step 5: Format check**

```bash
clang-format --dry-run --Werror samples/03_triangle/main.c
```

Expected: no diffs.

- [ ] **Step 6: Commit**

```bash
git add samples/03_triangle samples/CMakeLists.txt
git commit -m "add the 03_triangle sample"
```
