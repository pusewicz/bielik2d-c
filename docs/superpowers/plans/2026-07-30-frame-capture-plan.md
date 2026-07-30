# Frame Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `bk_gfx_request_capture(path)`, a fire-and-forget request that saves the
current frame's real swapchain content as a BMP once presented.

**Architecture:** A shared `bk__gfx_download_texture` internal helper does the whole
GPU-texture-to-CPU-buffer download (copy pass, submit, fence wait, map, heap-copy) and
is used both by a new pending-capture mechanism on `bk_gfx` (mirroring the existing
bind+draw pending slot) and by refactoring the existing golden-image test to use it
instead of its own inline copy of the same logic.

**Tech Stack:** C23, SDL3 GPU API, `SDL_Surface`/`SDL_SaveBMP`.

## Global Constraints

- Spec of record: `docs/superpowers/specs/2026-07-30-frame-capture-design.md` (read it
  before starting — this plan implements it, with one precision refinement over its
  illustrative code: pending-capture state is cleared unconditionally at the top of
  `bk__gfx_flush`, the same pattern already used for the pending pipeline/vertex-count
  state, not inline near the bottom as the spec's simplified code excerpt showed).
- Naming: public functions `bk_` + snake_case, internal linker-visible symbols `bk__`
  prefix, file-static functions/variables `s_` prefix.
- Every public symbol gets a doc comment.
- Errors: recoverable runtime failures (bad path, unsupported format, any SDL_GPU call
  failure) log via `SDL_Log` with a `"BK: "` prefix and the request is dropped — never
  a crash. `bk_gfx_request_capture(nullptr)` is a programmer-error precondition
  (`BK_ASSERT`), not a runtime failure.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.
- `.clang-format`: LLVM base, 4-space indent, 100 columns, `PointerAlignment: Right`,
  K&R attached braces. Run `clang-format -i` on every file you touch before committing.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`.
  Test: `ctest --test-dir build --output-on-failure`.
- Commit style: atomic commits, human-voice messages (no Conventional Commits prefixes
  like `feat:`/`fix:`, no AI signoff/Co-Authored-By footers).
- Only `SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM` and `SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM`
  are supported (the two documented SDR swapchain compositions) — anything else is a
  recoverable failure, not a crash.
- Use SDL's `_32` pixel-format aliases (`SDL_PIXELFORMAT_RGBA32`/`BGRA32`), never the
  packed `_8888` names, when wrapping downloaded GPU pixel bytes as an `SDL_Surface` —
  the packed names are bit-packed order, not byte-array order, and are flipped on
  little-endian (verified empirically during design; see the spec's §5 and §10).

---

## Task 1: `bk__gfx_download_texture` shared helper + refactor the golden-image test

**Files:**
- Modify: `src/internal/bk_gfx_internal.h`
- Modify: `src/bk_gfx.c`
- Modify: `tests/test_gfx_pipeline.c`

**Interfaces:**
- Produces: `void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *texture, Uint32 width, Uint32 height, SDL_GPUTextureFormat format)` — consumed by Task 2's frame-integration code and by this task's own test refactor.

This is a behavior-preserving refactor: the golden-image test's existing pixel
assertions are the safety net. There's no new user-facing behavior to red/green here
(the function only becomes truly exercised by real GPU download the moment the test
calls it) — the verification is that those existing, already-proven assertions still
pass unchanged after the extraction.

- [ ] **Step 1: Add the declaration to `src/internal/bk_gfx_internal.h`**

Add `#include <SDL3/SDL_gpu.h>` near the top (after the existing `#include
<bielik/bk_gfx.h>` and `#include <bielik/bk_gfx_pipeline.h>` lines — it's transitively
available via `bk_gfx_pipeline.h` already, but this header now uses SDL_GPU types
directly in its own declaration, so include what you use). Then append:

```c
/// Downloads width*height pixels (4 bytes/pixel) from texture via a copy pass added
/// to cmd, then submits cmd and waits for the GPU fence. cmd must not have been
/// submitted yet, and must not be used for anything else afterward -- this call
/// submits it on every path, success or failure. format must be
/// SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM or SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM (the
/// only 4-byte-per-pixel formats this helper supports; enforced by assertion, since
/// which format a caller passes is a programmer decision, not external data).
/// Returns a heap-allocated copy of the pixels (release with bk__free), or nullptr on
/// failure (logs via SDL_Log with a "BK: " prefix).
void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format);
```

- [ ] **Step 2: Implement it in `src/bk_gfx.c`**

Add `#include "internal/bk_app_internal.h"` to the top of `src/bk_gfx.c` (for
`bk__alloc`/`bk__free` — not currently included there). Then add the function
(anywhere in the file; end of file is fine):

```c
void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format) {
    BK_ASSERT(device != nullptr);
    BK_ASSERT(cmd != nullptr);
    BK_ASSERT(texture != nullptr);
    BK_ASSERT(format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
              format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = width * height * 4,
    };
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (transfer == nullptr) {
        SDL_Log("BK: SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return nullptr;
    }

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {.texture = texture, .w = width, .h = height, .d = 1};
    SDL_GPUTextureTransferInfo dst = {
        .transfer_buffer = transfer, .pixels_per_row = width, .rows_per_layer = height};
    SDL_DownloadFromGPUTexture(copy_pass, &src, &dst);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr) {
        SDL_Log("BK: SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
        SDL_Log("BK: SDL_WaitForGPUFences failed: %s", SDL_GetError());
        SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }
    SDL_ReleaseGPUFence(device, fence);

    void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        SDL_Log("BK: SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return nullptr;
    }

    size_t byte_size = (size_t)width * (size_t)height * 4;
    void *pixels = bk__alloc(byte_size);
    if (pixels != nullptr) {
        SDL_memcpy(pixels, mapped, byte_size);
    }

    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return pixels;
}
```

Note the transfer-buffer-creation failure path explicitly calls
`SDL_SubmitGPUCommandBuffer(cmd)` before returning `nullptr` — every return path in
this function submits `cmd` exactly once, since no copy pass was added yet on that
specific path and the caller must never submit it again.

Build to confirm it compiles: `cmake --build build`. It won't be exercised yet (Step 3
wires the only caller).

- [ ] **Step 3: Refactor `tests/test_gfx_pipeline.c` to use the helper**

Add `#include "internal/bk_gfx_internal.h"` to the top of `tests/test_gfx_pipeline.c`
(alongside the existing `#include "internal/bk_gfx_pipeline_internal.h"`).

In `test_draw_produces_expected_pixels`, replace this block:

```c
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
```

with:

```c
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

    void *pixels_buf = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(pixels_buf != nullptr);
    const uint8_t *pixels = (const uint8_t *)pixels_buf;
```

The `s_check_pixel` calls immediately after are unchanged. Later in the same function,
replace:

```c
    SDL_UnmapGPUTransferBuffer(device, transfer);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, offscreen);
    SDL_DestroyGPUDevice(device);
```

with:

```c
    bk__free(pixels_buf);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_ReleaseGPUTexture(device, offscreen);
    SDL_DestroyGPUDevice(device);
```

- [ ] **Step 4: Run the full test suite and verify it still passes**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `test_gfx_pipeline` with its existing
`test_draw_produces_expected_pixels` pixel assertions unchanged in value — this is
what proves the extraction preserved behavior exactly. If any pixel check fails, the
extraction introduced a bug; compare against the exact code in Steps 2-3 rather than
improvising a fix.

Also run: `clang-format --dry-run --Werror src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx_pipeline.c` — expect no diffs.

- [ ] **Step 5: Commit**

```bash
git add src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx_pipeline.c
git commit -m "extract a shared GPU texture download helper, and use it in the golden-image test"
```

---

## Task 2: `bk_gfx_request_capture` — pending state, frame integration, pixel-format mapping

**Files:**
- Modify: `include/bielik/bk_gfx.h`
- Modify: `src/internal/bk_gfx_internal.h`
- Modify: `src/bk_gfx.c`
- Modify: `tests/test_gfx.c`

**Interfaces:**
- Consumes: `bk__gfx_download_texture` (Task 1).
- Produces: `void bk_gfx_request_capture(const char *path)` — consumed by Task 3's
  sample/test usage. `const char *bk__gfx_get_pending_capture_path(void)` (internal,
  test-only) — consumed by this task's own test.

- [ ] **Step 1: Write the failing test in `tests/test_gfx.c`**

Add this function (after `test_last_set_wins`, before `test_bind_pipeline_and_draw_sets_pending_state`):

```c
static void test_request_capture_sets_pending_path(void) {
    bk_gfx_request_capture("screenshot.bmp");

    REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "screenshot.bmp") == 0);
}
```

Add a call to it in `main()`, right after `test_last_set_wins();` (before
`test_bind_pipeline_and_draw_sets_pending_state();`):

```c
    test_request_capture_sets_pending_path();
```

Then extend the existing `test_flush_early_return_clears_pending_state` (it already
proves `bk__gfx_flush`'s early-return path clears the pending pipeline/vertex-count
state without a real GPU device — this task's pending-capture state must be cleared
the same way, on the same path, so extending this test rather than duplicating a
second app-less-flush test is the right call). Change it from:

```c
static void test_flush_early_return_clears_pending_state(void) {
    int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);

    // No app has been booted in this test binary, so bk_gpu() returns nullptr and
    // SDL_AcquireGPUCommandBuffer fails immediately -- this exercises bk__gfx_flush's
    // early-return path (command-buffer acquire failure) without needing a real
    // window/GPU device, and proves pending state doesn't survive it.
    bk__gfx_flush();

    REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 0);
}
```

to:

```c
static void test_flush_early_return_clears_pending_state(void) {
    int dummy;
    BK_GfxPipeline *fake_pipeline = (BK_GfxPipeline *)&dummy;

    bk_gfx_bind_pipeline(fake_pipeline);
    bk_gfx_draw(3);
    bk_gfx_request_capture("unreachable.bmp");

    // No app has been booted in this test binary, so bk_gpu() returns nullptr and
    // SDL_AcquireGPUCommandBuffer fails immediately -- this exercises bk__gfx_flush's
    // early-return path (command-buffer acquire failure) without needing a real
    // window/GPU device, and proves pending state doesn't survive it.
    bk__gfx_flush();

    REQUIRE(bk__gfx_get_pending_pipeline() == nullptr);
    REQUIRE(bk__gfx_get_pending_vertex_count() == 0);
    REQUIRE(SDL_strcmp(bk__gfx_get_pending_capture_path(), "") == 0);
}
```

- [ ] **Step 2: Run the test and verify it fails to build**

Run: `cmake --build build --target test_gfx`

Expected: FAIL — undeclared identifier/undefined-reference errors for
`bk_gfx_request_capture` and `bk__gfx_get_pending_capture_path`.

- [ ] **Step 3: Add the public declaration to `include/bielik/bk_gfx.h`**

Append after `bk_gfx_draw`'s declaration:

```c
/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again
/// each frame you want captured. Failures (bad path, unsupported swapchain
/// composition) are logged via SDL_Log with a "BK: " prefix, not returned -- the
/// actual capture happens later, inside the frame's flush.
void bk_gfx_request_capture(const char *path);
```

- [ ] **Step 4: Add the internal test accessor to `src/internal/bk_gfx_internal.h`**

Append:

```c
/// Test-only accessor: returns the path set via bk_gfx_request_capture this frame, or
/// an empty string if none has been requested since the last flush.
const char *bk__gfx_get_pending_capture_path(void);
```

- [ ] **Step 5: Implement in `src/bk_gfx.c`**

Add the pending-capture state and its functions, after the existing
`bk__gfx_get_pending_vertex_count` definition:

```c
static char s_pending_capture_path[512];

void bk_gfx_request_capture(const char *path) {
    BK_ASSERT(path != nullptr);
    SDL_snprintf(s_pending_capture_path, sizeof s_pending_capture_path, "%s", path);
}

const char *bk__gfx_get_pending_capture_path(void) { return s_pending_capture_path; }

static SDL_PixelFormat s_pixel_format_for_gpu_format(SDL_GPUTextureFormat format) {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return SDL_PIXELFORMAT_RGBA32;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return SDL_PIXELFORMAT_BGRA32;
    default:
        return SDL_PIXELFORMAT_UNKNOWN;
    }
}
```

Now modify `bk__gfx_flush` itself. Change:

```c
void bk__gfx_flush(void) {
    static bool s_logged_acquire_failure = false;

    BK_GfxPipeline *pending_pipeline = s_pending_pipeline;
    int pending_vertex_count = s_pending_vertex_count;
    s_pending_pipeline = nullptr;
    s_pending_vertex_count = 0;

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
    if (!cmd) {
        if (!s_logged_acquire_failure) {
            SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            s_logged_acquire_failure = true;
        }
        return;
    }

    SDL_GPUTexture *tex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, nullptr, nullptr)) {
        SDL_Log("BK: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    if (!tex) {
        // minimized/occluded — nothing to draw into this frame
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    BK_Color c = bk__gfx_get_clear_color();
    SDL_GPUColorTargetInfo target = {
        .texture = tex,
        .clear_color = {c.r, c.g, c.b, c.a},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pending_pipeline));
        SDL_DrawGPUPrimitives(pass, (Uint32)pending_vertex_count, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
}
```

to:

```c
void bk__gfx_flush(void) {
    static bool s_logged_acquire_failure = false;

    BK_GfxPipeline *pending_pipeline = s_pending_pipeline;
    int pending_vertex_count = s_pending_vertex_count;
    char pending_capture_path[sizeof s_pending_capture_path];
    SDL_memcpy(pending_capture_path, s_pending_capture_path, sizeof pending_capture_path);
    s_pending_pipeline = nullptr;
    s_pending_vertex_count = 0;
    s_pending_capture_path[0] = '\0';

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(bk_gpu());
    if (!cmd) {
        if (!s_logged_acquire_failure) {
            SDL_Log("BK: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            s_logged_acquire_failure = true;
        }
        return;
    }

    Uint32 swap_w = 0, swap_h = 0;
    SDL_GPUTexture *tex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, &swap_w, &swap_h)) {
        SDL_Log("BK: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    if (!tex) {
        // minimized/occluded — nothing to draw into this frame
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    BK_Color c = bk__gfx_get_clear_color();
    SDL_GPUColorTargetInfo target = {
        .texture = tex,
        .clear_color = {c.r, c.g, c.b, c.a},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pending_pipeline != nullptr) {
        SDL_BindGPUGraphicsPipeline(pass, bk__gfx_pipeline_handle(pending_pipeline));
        SDL_DrawGPUPrimitives(pass, (Uint32)pending_vertex_count, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    if (pending_capture_path[0] != '\0') {
        SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
        SDL_PixelFormat sdl_format = s_pixel_format_for_gpu_format(format);
        if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
            SDL_Log("BK: bk_gfx_request_capture: unsupported swapchain format");
            SDL_SubmitGPUCommandBuffer(cmd);
        } else {
            void *pixels = bk__gfx_download_texture(bk_gpu(), cmd, tex, swap_w, swap_h, format);
            if (pixels != nullptr) {
                SDL_Surface *surface = SDL_CreateSurfaceFrom((int)swap_w, (int)swap_h, sdl_format,
                                                              pixels, (int)swap_w * 4);
                if (surface != nullptr) {
                    if (!SDL_SaveBMP(surface, pending_capture_path)) {
                        SDL_Log("BK: SDL_SaveBMP failed: %s", SDL_GetError());
                    }
                    SDL_DestroySurface(surface);
                } else {
                    SDL_Log("BK: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
                }
                bk__free(pixels);
            }
        }
    } else {
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}
```

Every path through this function submits `cmd` exactly once: the three early returns
already did before (unchanged); the new capture branch either submits explicitly
(unsupported format) or via `bk__gfx_download_texture` (which always submits
internally, per Task 1); the no-capture `else` branch submits as before. Double-check
this invariant holds before moving on — it's the easiest thing to get wrong here.

- [ ] **Step 6: Run the test and verify it passes**

Run: `cmake --build build --target test_gfx && ./build/tests/test_gfx`

Expected: prints `test_gfx: OK` and exits 0.

- [ ] **Step 7: Full clean build + test suite, format check**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
```

Expected: build succeeds, all tests pass, no format diffs. (The new capture branch in
`bk__gfx_flush` isn't exercised by any test yet — that's Task 3's job, matching how
sub-project 1's Task 3 bind+draw integration wasn't proven end-to-end until Task 4's
golden-image test. `test_gfx`'s state-only tests only prove the pending-state
mechanics.)

- [ ] **Step 8: Commit**

```bash
git add include/bielik/bk_gfx.h src/internal/bk_gfx_internal.h src/bk_gfx.c tests/test_gfx.c
git commit -m "add bk_gfx_request_capture"
```

---

## Task 3: End-to-end capture test + CI wiring

**Files:**
- Create: `tests/test_gfx_capture.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `bk_gfx_request_capture` (Task 2).

Unlike Tasks 1-2's headless/state-only tests, this needs a real claimed window and
swapchain — there's no way to test swapchain capture without one. Follows
`tests/test_app_lifecycle.c`'s pattern (a real, small `bk_run()`-booted app).

- [ ] **Step 1: Write `tests/test_gfx_capture.c`**

```c
#include "bk_test.h"
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <stdio.h>
#include <stdlib.h>

static int s_update_calls = 0;
static bool s_capture_requested = false;
static char s_capture_path[512];

static BK_Result test_init(void **state, int argc, char **argv) {
    (void)state;
    (void)argc;
    (void)argv;
    REQUIRE(bk_window() != nullptr);
    REQUIRE(bk_gpu() != nullptr);

    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);
    SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_capture_output.bmp",
                 base_path);
    SDL_RemovePath(s_capture_path); // in case a prior failed run left one behind
    return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *f) {
    (void)state;
    (void)f;
    s_update_calls++;
    if (s_update_calls >= 3) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *f) {
    (void)state;
    (void)f;
    bk_gfx_set_clear_color((BK_Color){.r = 0.2f, .g = 0.4f, .b = 0.6f, .a = 1.0f});
    // Request on the first render call reached, whichever tick that lands on --
    // robust to fixed-tick batching (multiple ticks can run before one render call).
    if (!s_capture_requested) {
        bk_gfx_request_capture(s_capture_path);
        s_capture_requested = true;
    }
}

int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .window = {.title = "test_gfx_capture", .w = 64, .h = 64},
        .time = {.tick_hz = 60},
        .init = test_init,
        .update = test_update,
        .render = test_render,
    };
    int result = bk_run(&desc, argc, argv);
    REQUIRE(result == 0);
    REQUIRE(s_capture_requested);

    SDL_Surface *loaded = SDL_LoadBMP(s_capture_path);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->w == 64);
    REQUIRE(loaded->h == 64);

    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(rgba != nullptr);
    SDL_DestroySurface(loaded);

    const uint8_t *pixels = (const uint8_t *)rgba->pixels;
    constexpr int tolerance = 5;
    size_t center = ((size_t)(rgba->h / 2) * (size_t)rgba->pitch) + (size_t)(rgba->w / 2) * 4;
    REQUIRE(abs((int)pixels[center + 0] - 51) <= tolerance);  // R = 0.2 * 255
    REQUIRE(abs((int)pixels[center + 1] - 102) <= tolerance); // G = 0.4 * 255
    REQUIRE(abs((int)pixels[center + 2] - 153) <= tolerance); // B = 0.6 * 255
    REQUIRE(abs((int)pixels[center + 3] - 255) <= tolerance); // A

    SDL_DestroySurface(rgba);
    SDL_RemovePath(s_capture_path);
    printf("test_gfx_capture: OK\n");
    return 0;
}
```

This checks real pixel values read back from the saved file — exactly the kind of
check that would have caught the R/B-channel-swap trap from the spec's §5 immediately,
rather than a test that only checks "a file was created."

- [ ] **Step 2: Wire the test into `tests/CMakeLists.txt`**

Append:

```cmake
add_executable(test_gfx_capture test_gfx_capture.c)
target_link_libraries(test_gfx_capture PRIVATE bielik bk_warnings)
add_test(NAME test_gfx_capture COMMAND test_gfx_capture)
set_tests_properties(test_gfx_capture PROPERTIES TIMEOUT 30)
```

(No `bk_stage_shaders` call needed — this test draws nothing beyond the clear color,
no pipeline/shaders involved.)

- [ ] **Step 3: Run the test and verify it passes**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build --target test_gfx_capture
./build/tests/test_gfx_capture
```

Expected: prints `test_gfx_capture: OK` and exits 0. If a pixel check fails, first
confirm which of R/G/B is off and by how much — a channel swap (R and B transposed)
means the `_32` vs `_8888` pixel-format mapping picked the wrong alias for this
machine's actual swapchain format; a uniformly-off value suggests a tolerance or
clear-color mismatch instead.

- [ ] **Step 4: Add the test to CI's GPU-dependent (allow-fail) group**

This test creates a real window and claims a real GPU device via `bk_run()` — same
category as `test_app_lifecycle` and `test_gfx_pipeline`. In
`.github/workflows/ci.yml`, extend all six existing occurrences of the pattern
`"test_app_lifecycle|test_gfx_pipeline"` to `"test_app_lifecycle|test_gfx_pipeline|test_gfx_capture"`:

- `-E "test_app_lifecycle|test_gfx_pipeline"` — 2 occurrences: `build-and-test`'s main `Test` step, `debug-sanitizers`'s `Test` step.
- `-R "test_app_lifecycle|test_gfx_pipeline"` — 4 occurrences: `build-and-test`'s Ubuntu and macOS/Windows GPU-dependent steps, `debug-sanitizers`'s Ubuntu and macOS GPU-dependent steps.

- [ ] **Step 5: Full clean build + test suite, format check**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror tests/test_gfx_capture.c
```

Expected: build succeeds, all 9 tests pass (the 7 from before plus `test_gfx_capture`
and unchanged `test_gfx_pipeline`), no format diffs.

- [ ] **Step 6: Commit**

```bash
git add tests/test_gfx_capture.c tests/CMakeLists.txt .github/workflows/ci.yml
git commit -m "add an end-to-end test for bk_gfx_request_capture"
```
