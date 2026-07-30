# Bielik2D — Frame Capture Design

## 0. Context and scope

GitHub issue [#3](https://github.com/pusewicz/bielik2d-c/issues/3) requests a way to save
the current frame to disk directly from `bk_gfx`, motivated by a concrete pain point hit
while verifying `samples/03_triangle` visually: OS-level whole-screen capture is fragile
(wrong-window captures, doesn't work in a headless session at all).

This spec covers exactly one capability: **capturing the real swapchain** — what's
actually about to be presented — as a BMP file, on request, from any `render()` callback.
It builds directly on Phase 2 sub-project 1 (the `bk_gfx_pipeline` module and its
golden-image test, [PR #1](https://github.com/pusewicz/bielik2d-c/pull/1)), reusing the
exact GPU-texture-download technique already proven there.

## 1. Module boundaries and file layout

No new module. This extends the existing `bk_gfx` module (already the module that owns
the swapchain and the per-frame flush) and adds one new internal helper it and the
existing test share:

```
bielik2d/
  include/bielik/
    bk_gfx.h                 (add: bk_gfx_request_capture)
  src/
    bk_gfx.c                 (add: pending-capture state, flush-time capture,
                               bk__gfx_download_texture, format-mapping helper)
    internal/
      bk_gfx_internal.h      (add: bk__gfx_download_texture declaration)
  tests/
    test_gfx_capture.c       (new)
    test_gfx_pipeline.c      (refactored: golden-image test uses the shared helper
                               instead of its own inline download code)
```

## 2. Public API — additions to `bk_gfx.h`

```c
/// Requests that the frame currently being rendered be saved as a BMP to path once
/// presented. path is copied internally (safe to pass a stack buffer built fresh each
/// frame) -- the request is consumed after the frame it applies to, so call again each
/// frame you want captured. Failures (bad path, unsupported swapchain composition) are
/// logged via SDL_Log with a "BK: " prefix, not returned -- the actual capture happens
/// later, inside the frame's flush, after this function has already returned.
void bk_gfx_request_capture(const char *path);
```

`path` is copied into a fixed-size internal buffer (`char[512]`, matching the path-buffer
size already used elsewhere in this codebase), not stored as a pointer. Storing a raw
pointer the way `bk_gfx_bind_pipeline` stores its `BK_GfxPipeline *` would be a
dangling-pointer bug here: pipelines are long-lived objects, but a capture path is
typically built fresh on the stack inside `render()`, whose frame is gone by the time
`bk__gfx_flush` actually consumes the request later that same frame.

`bk_gfx_request_capture(nullptr)` is a programmer error (`BK_ASSERT`), not a runtime
failure — unlike a bad *path string*, which is caller-supplied data and handled via the
log-and-drop convention below.

## 3. Shared download helper — addition to `bk_gfx_internal.h`

```c
/// Downloads width*height pixels (4 bytes/pixel -- only 4-byte-per-pixel SDR formats
/// are supported) from texture via a copy pass added to cmd, then submits cmd and
/// waits for the GPU fence. cmd must not have been submitted yet, and must not be used
/// for anything else afterward (this call submits it). Returns a heap-allocated copy
/// of the downloaded pixels (release with bk__free), or nullptr on failure (logs via
/// SDL_Log with a "BK: " prefix).
void *bk__gfx_download_texture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd,
                               SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                               SDL_GPUTextureFormat format);
```

This owns the entire copy-pass → submit-with-fence → wait → map → heap-copy → unmap →
release-transfer-buffer sequence and hands back a plain, fully-owned buffer — no GPU
handles for the caller to manage afterward. `bk_gfx_internal.h` needs a new
`#include <SDL3/SDL_gpu.h>` for the GPU types this declaration uses (it currently has
none).

Both current and future callers get real value from this: it removes ~15 lines of
transfer-buffer/fence/map boilerplate that `tests/test_gfx_pipeline.c`'s golden-image
test currently has inline (see §6), and it's what the new capture path uses too.

## 4. Frame integration

The request side, in `bk_gfx.c`:

```c
static char s_pending_capture_path[512];

void bk_gfx_request_capture(const char *path) {
    BK_ASSERT(path != nullptr);
    SDL_snprintf(s_pending_capture_path, sizeof s_pending_capture_path, "%s", path);
}
```

`SDL_snprintf` truncates (never overflows) if `path` is 512 bytes or longer; a
truncated path is caller error (an unreasonably long path), not something this
function silently corrects further — the resulting `SDL_SaveBMP` call in `bk__gfx_flush`
will simply fail against the truncated path and log via the existing failure path in
§8, same as any other bad path.

The flush side, consuming it: the helper submits `cmd` itself (it needs a fence to
wait on), so `bk__gfx_flush` must call *either* the helper *or* its own plain
`SDL_SubmitGPUCommandBuffer` for a given frame — never both, since a command buffer
can't be submitted twice.

`SDL_WaitAndAcquireGPUSwapchainTexture`'s width/height out-params, currently passed as
`nullptr` (unused before now), need to be captured into locals — the capture path needs
the swapchain's actual dimensions.

```c
void bk__gfx_flush(void) {
    ...
    Uint32 swap_w = 0, swap_h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, bk_window(), &tex, &swap_w, &swap_h)) {
        ...
    }
    ...
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (pending_pipeline != nullptr) { ... }
    SDL_EndGPURenderPass(pass);

    if (s_pending_capture_path[0] != '\0') {
        SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window());
        void *pixels = bk__gfx_download_texture(bk_gpu(), cmd, tex, swap_w, swap_h, format);
        if (pixels != nullptr) {
            SDL_PixelFormat sdl_format = s_pixel_format_for_gpu_format(format);
            if (sdl_format != SDL_PIXELFORMAT_UNKNOWN) {
                SDL_Surface *surface =
                    SDL_CreateSurfaceFrom((int)swap_w, (int)swap_h, sdl_format, pixels, (int)swap_w * 4);
                if (surface != nullptr) {
                    if (!SDL_SaveBMP(surface, s_pending_capture_path)) {
                        SDL_Log("BK: SDL_SaveBMP failed: %s", SDL_GetError());
                    }
                    SDL_DestroySurface(surface);
                } else {
                    SDL_Log("BK: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
                }
            } else {
                SDL_Log("BK: bk_gfx_request_capture: unsupported swapchain format");
            }
            bk__free(pixels);
        }
        s_pending_capture_path[0] = '\0'; // consumed either way
    } else {
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}
```

## 5. Pixel format mapping — a real correctness trap, verified empirically

SDL_GPU's documented "SDR" swapchain compositions are `R8G8B8A8_UNORM` or
`B8G8R8A8_UNORM`. Empirically confirmed against a real Metal device on this dev machine
(clearing to R=51,G=102,B=153 and reading back bytes `[153,102,51,255]`): this Metal
swapchain uses `B8G8R8A8_UNORM` (byte order B,G,R,A in memory).

Mapping this to the right `SDL_PixelFormat` for `SDL_CreateSurfaceFrom` has a sharp edge:
SDL's `_32` aliases (`SDL_PIXELFORMAT_RGBA32`, `SDL_PIXELFORMAT_BGRA32`, ...) are
endianness-corrected to mean "these bytes in memory, in this literal sequence" — matching
how GPU/Vulkan-style formats like `R8G8B8A8_UNORM` describe byte order. The plain packed
names (`SDL_PIXELFORMAT_RGBA8888`, `SDL_PIXELFORMAT_BGRA8888`, ...) describe *bit-packed
uint32* order instead, which is **reversed from the byte-array meaning on little-endian**
platforms (confirmed by reading `SDL_pixels.h`'s `#if SDL_BYTEORDER == SDL_BIG_ENDIAN`
block defining these aliases). Using the packed name instead of the `_32` alias would
compile fine and silently produce a channel-swapped screenshot — exactly the kind of bug
that's easy to ship unnoticed.

```c
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

Only these two formats are handled; anything else (HDR/scRGB compositions) hits the
`SDL_PIXELFORMAT_UNKNOWN` branch in §4 and logs-and-drops rather than producing a wrong
or corrupted file.

## 6. Refactoring the golden-image test to use the shared helper

`tests/test_gfx_pipeline.c`'s `test_draw_produces_expected_pixels` currently does its own
inline transfer-buffer/copy-pass/fence/map sequence (lines ~154-201 as of `a20448b`).
This gets replaced with a single call:

```c
    SDL_EndGPURenderPass(pass);

    void *pixels = bk__gfx_download_texture(device, cmd, offscreen, size, size,
                                            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(pixels != nullptr);

    s_check_pixel((const uint8_t *)pixels, size, size / 2, size / 2, 255, 0, 0, 255, tolerance);
    ... (remaining s_check_pixel calls, unchanged) ...

    bk__free(pixels);
```

removing the now-redundant manual transfer-buffer creation, copy pass, fence
acquire/wait/release, and map/unmap — `bk__gfx_download_texture` owns all of that. The
test's existing pixel-value assertions are untouched, so this refactor is behavior-
preserving by construction: if the extraction got the download logic wrong, the test's
own existing (already-proven, mutation-tested per Task 4) assertions catch it immediately.

## 7. Testing — `tests/test_gfx_capture.c`

Unlike the golden-image test (headless, offscreen texture, no window at all), capture
inherently needs a real claimed window and swapchain — there's no way to test swapchain
capture without one. This test follows `test_app_lifecycle.c`'s pattern instead: boot a
real (small, hidden) app via `bk_run()`, call `bk_gfx_request_capture(path)` from
`render()` on one tick, let the app run a couple more frames, quit, then read the saved
file back with `SDL_LoadBMP` and check actual pixel values against the known clear color
within a small tolerance — the same kind of real-pixel-value check that would have caught
the R/B-channel-swap trap in §5 immediately, rather than a test that only checks "a file
was created."

Save path: build under `SDL_GetBasePath()` (matching how shaders are already located) so
the test doesn't depend on process working directory; delete the file at the end of the
test regardless of pass/fail.

Like `test_app_lifecycle`, this needs a real GPU/display and joins the existing
GPU-dependent, `continue-on-error` CI bucket in `.github/workflows/ci.yml`
(`-E "test_app_lifecycle|test_gfx_pipeline|test_gfx_capture"` for the required step,
`-R "test_app_lifecycle|test_gfx_pipeline|test_gfx_capture"` for the GPU-dependent one),
not the required step — matching the same start-conservative, promote-later policy
already established for both existing GPU-dependent tests.

## 8. Error handling

Every failure point — transfer buffer creation, submit, fence wait, an unsupported
swapchain format, `SDL_CreateSurfaceFrom`, `SDL_SaveBMP` — logs via `SDL_Log` with a
`"BK: "` prefix and the pending request is dropped (cleared) regardless of outcome.
Never a crash, never a silent no-op, never an automatic retry on a later frame.
`bk_gfx_request_capture(nullptr)` is the one precondition treated as a programmer error
(`BK_ASSERT`), matching how null-pointer arguments are handled elsewhere in this
codebase (e.g. `bk_gfx_bind_pipeline`).

## 9. Explicitly out of scope

- **Capturing anything other than the swapchain** — offscreen canvases/render targets
  don't exist until Phase 2 sub-project 3; `bk__gfx_download_texture` itself is already
  generic enough to support that later without changes.
- **Any swapchain format beyond the two documented SDR compositions** — HDR/scRGB
  capture would need a real pixel-value transform (tone mapping), not just a format
  relabel; out of scope until a concrete need exists.
- **PNG or any format beyond BMP** — decided in favor of zero new dependencies
  (`SDL_SaveBMP` is already part of SDL3); revisit only if file size becomes a real
  complaint.
- **Capturing every frame / video capture** — this is a per-request, occasional-use
  feature (like a player-facing screenshot button), not a recording tool; the GPU sync
  point it forces (`SubmitGPUCommandBufferAndAcquireFence` + wait) is a real cost meant
  to be paid rarely, not every frame.

## 10. Decisions and rationale (do not relitigate in implementation sessions)

- **Capture the real swapchain, not a dedicated offscreen redirect**: captures exactly
  what's on screen, and works via the same render path every frame already takes — no
  duplicate rendering, no `--screenshot`-mode branch in samples. Confirmed empirically
  that the SDL_GPU docs' "swapchain texture is write-only, cannot be used as a sampler"
  restriction is about shader-visible reads, not copy-pass downloads (verified against
  the Metal backend's blit-encoder-based `METAL_DownloadFromTexture` implementation and
  a real minimal reproduction on this dev machine) — so this approach is genuinely
  implementable, not blocked by that restriction as first appeared.
- **Permanent public API, not a debug-only utility**: a screenshot feature is something
  real shipped games want too (bug reports, sharing), and correct-by-default costs
  nothing extra to build now.
- **BMP via `SDL_SaveBMP`, not PNG**: zero new dependencies, matches this project's
  stated minimalism. Revisit only if file size becomes a real complaint.
- **Shared `bk__gfx_download_texture` helper, refactoring the existing golden-image
  test to use it**: this is genuinely the same GPU operation in two places, not
  superficially similar code (unlike the accepted sample-vs-test shader-loading
  duplication from sub-project 1) — and one of the two copies is now production code,
  where duplication-drift risk matters more. Low risk: the test's own existing,
  already-mutation-tested assertions catch any refactor mistake immediately.
- **`bk_gfx_request_capture` copies its path argument into a fixed internal buffer**
  rather than storing a pointer: a capture path is typically built fresh on the stack
  inside `render()`, whose frame is gone by the time `bk__gfx_flush` consumes the
  request later that same frame — storing a raw pointer here (unlike
  `bk_gfx_bind_pipeline`'s long-lived `BK_GfxPipeline *`) would be a dangling-pointer
  bug.
- **The download helper submits its command buffer internally** rather than leaving
  that to the caller: it needs a fence to wait on regardless, so owning submit keeps
  both call sites simpler. This does mean `bk__gfx_flush` must branch between the
  helper and its own plain submit rather than doing both — documented explicitly in §4
  since it's an easy mistake (double-submitting a command buffer) if missed.
- **Pixel format mapping uses SDL's `_32` aliases (`RGBA32`/`BGRA32`), not the packed
  `_8888` names**: verified empirically that using the wrong one silently produces a
  channel-swapped image on little-endian platforms — documented in §5 with the actual
  measured bytes that exposed this, specifically so a future reader doesn't "simplify"
  this back to the seemingly-more-obvious packed name.
