# Bielik2D — Phase 2, Sub-project 3: Canvases / Render Targets + Resize Handling + Depth-Stencil

## 0. Context and scope

`PLAN.md` §7 scopes Phase 2 ("P2 gfx core") as: pipelines, buffers, textures, offline
shader compile, canvases/render targets. Sub-project 1 (shader toolchain + graphics
pipelines) and sub-project 2 (buffers, textures, compute pipelines — spec
`docs/superpowers/specs/2026-07-30-phase2-buffers-textures-compute-design.md`) shipped
first; that spec's §0 named this sub-project as the last slice, and its §10 explicitly
deferred "canvases/render targets, depth-stencil, resize handling" here. Landing this
closes Phase 2 entirely and unblocks P3 (draw2d).

Three gaps this sub-project closes:

- **No render target but the swapchain.** `bk__gfx_flush` (`src/bk_gfx.c`) hardcoded
  one render pass against the swapchain texture. `BK_GfxTextureUsage` had no
  `COLOR_TARGET` variant — the golden-image tests in sub-project 1/2 hand-rolled
  `SDL_CreateGPUTexture` to get one.
- **No depth-stencil anywhere.** `SDL_BeginGPURenderPass(cmd, &target, 1, nullptr)` —
  that trailing `nullptr` was the depth-stencil slot. `BK_GfxPipelineDesc` had no
  depth fields.
- **No resize handling and no way to ask the window's size.** No `SDL_EVENT_WINDOW_*`
  handling anywhere; `swap_w`/`swap_h` came back from
  `SDL_WaitAndAcquireGPUSwapchainTexture` each frame and were discarded. `PLAN.md` §8
  listed "explicit resize events handling arrives with P2".

Cute Framework (the project's donor-code reference, `/Users/piotr/Work/GitHub/pusewicz/cute_framework`)
solved all three; its `CF_Canvas`/`CF_RenderState`/resize-event handling informed the
design below, including several things it gets wrong that this sub-project does
differently (see §9).

## 1. Two decisions that shape the whole design

**Canvases redirect the frame's existing single render pass, rather than adopting
Cute Framework's always-render-offscreen model.** CF never renders into the
swapchain — every frame goes into an offscreen canvas, blitted to the swapchain once.
That's clean, but it isn't additive here: Bielik2D's existing samples pass
`.color_target_format = SDL_GetGPUSwapchainTextureFormat(...)`, and
`bk_gfx_request_capture`'s existing contract captures the swapchain. Redirecting the
pass only when `bk_gfx_bind_canvas` is called keeps both untouched when it isn't.

**Depth-stencil is opt-in per pipeline (`BK_GfxPipelineDesc.depth_stencil_format`),
not derived from the bound canvas.** CF derives a pipeline's `target_info` from the
currently-bound canvas at pipeline-build time — comment at
`cute_graphics_sdlgpu.cpp:1643-1645`: *"Always declare depth-stencil target if the
canvas has one, regardless of whether the material enables depth/stencil testing."*
CF can only do that because its pipelines are built lazily from a format-keyed cache
(`CF_PipelineKey`, `cute_graphics_sdlgpu.cpp:62-92`). Bielik2D creates pipelines
eagerly, with an explicit `color_target_format`, in `bk_gfx_pipeline_create` — no
cache. Deriving depth from the canvas would mean turning on `BK_WindowDesc.depth_stencil`
retroactively breaks every already-shipped pipeline with an opaque SDL_GPU validation
error the moment it's used in a depth-enabled pass. `bk_gfx_pipeline.h`'s existing
comment already anticipated this model: *"an offscreen texture's own format for
headless/canvas use."*

## 2. New module: `bk_gfx_canvas`

`include/bielik/bk_gfx_canvas.h` + `src/bk_gfx_canvas.c` +
`src/internal/bk_gfx_canvas_internal.h`.

```c
typedef struct BK_GfxCanvas BK_GfxCanvas;

typedef struct BK_GfxCanvasDesc {
    i32 width, height;
    bool depth_stencil;        // allocate a depth-stencil attachment
    BK_GfxFilter blit_filter;  // filter used when blitting to the swapchain (0 == NEAREST)
} BK_GfxCanvasDesc;

BK_GfxCanvas *bk_gfx_canvas_create(SDL_GPUDevice *device, const BK_GfxCanvasDesc *desc);
void bk_gfx_canvas_destroy(BK_GfxCanvas *canvas);

/// The canvas's color attachment, bindable via bk_gfx_bind_texture like any other
/// sampled texture.
BK_GfxTexture *bk_gfx_canvas_texture(BK_GfxCanvas *canvas);
void bk_gfx_canvas_size(const BK_GfxCanvas *canvas, i32 *out_width, i32 *out_height);

/// The depth-stencil format this device supports, probed D24_UNORM_S8_UINT ->
/// D32_FLOAT_S8_UINT -> D16_UNORM. Pipelines drawing into a depth-enabled pass must
/// pass this as BK_GfxPipelineDesc.depth_stencil_format.
SDL_GPUTextureFormat bk_gfx_depth_stencil_format(SDL_GPUDevice *device);
```

The probe order and rationale are CF's (`cute_graphics.cpp:600-606`), and the reason
is a real bug its own comment names: *Metal has no D24S8; without the D32S8 fallback
canvases silently drop to D16 and lose stencil entirely.* SDL_gpu.h's own docs confirm
the guarantee this probe relies on: D16_UNORM plus one (not necessarily both) of
D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT is supported for `DEPTH_STENCIL_TARGET` usage on
every backend.

A canvas is built by composing two calls to the existing `bk_gfx_texture_create`
(color via a new `BK_GFX_TEXTURE_USAGE_RENDER_TARGET`, depth via a new
`BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL`) rather than duplicating SDL_GPU texture-creation
logic — see §3.

Stencil *format* is selected where the device supports it, but no stencil test/op API
is exposed this sub-project — a format that's ready when a consumer appears is not the
same as shipping a speculative surface with none.

## 3. `bk_gfx_texture` gains two usages

`BK_GfxTextureUsage` grows:

- `BK_GFX_TEXTURE_USAGE_RENDER_TARGET` → `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SAMPLER`,
  `R8G8B8A8_UNORM`. Sampleable so a rendered canvas can also be drawn as an ordinary
  textured quad, and so `SDL_BlitGPUTexture` can read it.
- `BK_GFX_TEXTURE_USAGE_DEPTH_STENCIL` → `SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET`,
  format from `bk_gfx_depth_stencil_format(device)` instead of the hardcoded
  `R8G8B8A8_UNORM` every other usage gets. No sampler bit (CF's rule: depth textures
  don't need one, the color attachment's sampler covers sampling needs).

`src/internal/bk_gfx_texture_internal.h` gains `bk__gfx_texture_format`, so the canvas
and pipeline-format-assert code (§5) can read back what format a texture actually got.

## 4. `bk_gfx_pipeline` gains depth state

```c
typedef enum BK_GfxCompare {
    BK_GFX_COMPARE_ALWAYS,   // 0 -- a zeroed BK_GfxPipelineDesc means "no depth test"
    BK_GFX_COMPARE_NEVER, BK_GFX_COMPARE_LESS, BK_GFX_COMPARE_LESS_EQUAL,
    BK_GFX_COMPARE_GREATER, BK_GFX_COMPARE_GREATER_EQUAL,
    BK_GFX_COMPARE_EQUAL, BK_GFX_COMPARE_NOT_EQUAL,
} BK_GfxCompare;

// added to BK_GfxPipelineDesc:
SDL_GPUTextureFormat depth_stencil_format;  // INVALID (0) => pass has no depth attachment
BK_GfxCompare depth_compare;
bool depth_write;
```

`ALWAYS = 0` first, so every pipeline created before this sub-project (samples
03/04/05, sub-project 1/2's tests) is unaffected without editing a single call site —
the same zero-is-safe enum ordering CF uses for its own compare/stencil-op enums.

The depth test is enabled by `depth_write || depth_compare != ALWAYS`, not by
`depth_write` alone — CF's own header claims the latter
(`cute_graphics.h:1960-1961`: *"Must be true to enable depth-testing"*) but its
implementation does the former (`cute_graphics_sdlgpu.cpp:1709`). Only the latter lets
a pipeline depth-*test* without depth-*writing* (an overlay that reads occlusion but
never contributes to it).

`bk__gfx_pipeline_depth_format` (new internal accessor) lets the frame flush assert a
bound pipeline's declared format against the pass it's drawn into — see §5.

## 5. Frame integration: `bk_gfx_bind_canvas`

```c
void bk_gfx_bind_canvas(BK_GfxCanvas *canvas);
```

Redirects the frame's one render pass to `canvas` (color + depth, if the canvas has
one) instead of the swapchain; the binding is consumed by the frame's flush, same
convention as every other `bk_gfx_bind_*`. Once the pass ends, the canvas is blitted
onto the swapchain (`SDL_BlitGPUTexture`, stretched to fit, filtered per
`BK_GfxCanvasDesc.blit_filter`) — a canvas smaller than the window is the
fixed-internal-resolution/pixel-art path.

The blit is what keeps this additive at the API-contract level, not just the
call-site level: without it, a frame with a canvas bound would present a cleared
swapchain, and `bk_gfx_request_capture` (which downloads the swapchain) would capture
that clear instead of the canvas contents. With it, capture continues to mean what its
doc comment already says.

`bk__gfx_flush`'s restructure (`src/bk_gfx.c`):

1. snapshot+clear pending state (existing block, now including the canvas slot)
2. acquire cmd buffer, acquire swapchain (`swap_w`/`swap_h`) — existing early-outs for
   acquire failure and minimized/occluded (`tex == nullptr`) are untouched
3. **target selection**: canvas bound → its color+depth attachments; else → the
   swapchain, plus the framework-owned swapchain depth texture if
   `BK_WindowDesc.depth_stencil` requested one (§6)
4. one render pass — same bind/draw body as before, now with a
   `SDL_GPUDepthStencilTargetInfo *` that's `nullptr` when there's no attachment, and
   a `BK_ASSERT` that a bound pipeline's `depth_stencil_format` matches the pass's
   actual attachment format. This turns SDL_GPU's opaque "pipeline incompatible with
   render pass" validation failure into a named assertion at the exact call site.
5. canvas bound → blit it onto the swapchain
6. capture path and submit — unchanged (now captures post-blit swapchain contents)

## 6. Swapchain depth + resize

`BK_WindowDesc` gains `bool depth_stencil`. When set, `bk_gfx` owns a file-static
depth texture, created lazily on the first flush and **recreated whenever the flush's
own `swap_w`/`swap_h` no longer match its current size** — not off a resize-event
dirty flag. Those values are authoritative for what's actually being rendered into
this frame; the check is inherently coalesced (at most one recreate per frame no
matter how many resize events a border drag fires — CF has no such coalescing,
recreating on every single event); and the minimized-window 0×0 case never reaches it,
because the existing `tex == nullptr` early-out fires first (CF's canvas-recreate path
has no equivalent guard and can construct a canvas at 0×0). No `SDL_WaitForGPUIdle` on
recreate: SDL_GPU defers a released texture's destruction past any command buffer
still referencing it, same reasoning CF's own canvas-destroy relies on.

`bk_window_size(i32 *out_w, i32 *out_h)` (new, `bk_app.h`) reports the window's
drawable size in pixels — equal to its logical size today, since Bielik2D doesn't
request a high-DPI window (`SDL_WINDOW_HIGH_PIXEL_DENSITY`), so DPI-aware scaling
(CF's `pixel_scale` machinery) is out of scope. Cached in `BK_AppState`, seeded at
boot, refreshed in `bk__event` on both `SDL_EVENT_WINDOW_RESIZED` and
`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` — handled identically, since which one a given
platform fires isn't guaranteed (the same reasoning behind CF's own dual-event
handling, `cute_input.cpp:502-510`).

**Insertion point matters:** the refresh runs at the top of `bk__event`, before the
`if (s_app.desc.event)` branch that would otherwise return early and skip it for any
app supplying a custom `.event` handler. It doesn't consume the event either — a
custom handler still sees `SDL_EVENT_WINDOW_RESIZED` exactly as before.

## 7. Files

New: `include/bielik/bk_gfx_canvas.h`, `src/bk_gfx_canvas.c`,
`src/internal/bk_gfx_canvas_internal.h`, `shaders/depth_tri.{vert,frag}` (+ committed
`.spv`/`.msl`), `samples/06_canvas/{CMakeLists.txt,main.c}`,
`tests/test_gfx_canvas.c`, `tests/test_gfx_resize.c`, `tests/test_header_bk_gfx_canvas.c`.

Modified: `src/bk_gfx.c`, `include/bielik/bk_gfx.h`, `src/internal/bk_gfx_internal.h`,
`src/bk_gfx_texture.c`, `include/bielik/bk_gfx_texture.h`,
`src/internal/bk_gfx_texture_internal.h`, `src/bk_gfx_pipeline.c`,
`include/bielik/bk_gfx_pipeline.h`, `src/internal/bk_gfx_pipeline_internal.h`,
`src/bk_app.c`, `include/bielik/bk_app.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
`samples/CMakeLists.txt`, `.github/workflows/ci.yml`.

## 8. Testing

Golden-image depth test (`tests/test_gfx_canvas.c`): renders two triangles at
different depths, each sized to cover the *entire* canvas (the standard
over-sized-triangle rasterizer-clip trick), into a real `BK_GfxCanvas` built through
`bk_gfx_canvas_create` — not a hand-rolled `SDL_CreateGPUTexture`, so the test also
covers the canvas's attachment pairing, not just SDL_GPU's raw depth behavior. Run
twice with the two triangles' order swapped in the vertex buffer; the near one must
win everywhere both times. Draw order alone would pass trivially even with a broken
depth test (`compare = ALWAYS` makes the *last-drawn* triangle win, which happens to
match one of the two orders) — only the both-orders form actually discriminates. This
was verified directly: temporarily breaking `depth_compare` to `ALWAYS` was confirmed
to fail the test before restoring it.

Resize test (`tests/test_gfx_resize.c`): drives a real window via `bk_run`, requests a
resize mid-run via `SDL_SetWindowSize`, and checks the invariant that survives even if
a headless/CI window manager doesn't honor the request: `bk_window_size` agrees with
SDL's own drawable-size query, and the framework swapchain depth texture's size
matches both — not whatever size it was created at, and not the requested size if the
WM ignored it.

`samples/06_canvas`: two overlapping triangles (near red, far blue, drawn far-first on
purpose) into a 160×90 canvas with depth, `NEAREST`-blitted onto a resizable window.
Verified visually via a temporary `bk_gfx_request_capture` call (not committed): red
correctly occludes blue in the overlap region, and the upscale is crisply blocky.

## 9. Decisions and rationale (do not relitigate in implementation sessions)

- **No stencil test/op API.** Format support is probed and available; no
  `BK_GfxStencil*` surface ships without a consumer.
- **No MSAA.** CF's canvas carries a third resolve texture and strips `SAMPLER` usage
  from MSAA color targets; none of that exists until something asks for it.
- **No DPI-aware sizing.** The window is created without
  `SDL_WINDOW_HIGH_PIXEL_DENSITY`; points == pixels. CF's `pixel_scale` conversion
  layer stays closed.
- **One canvas per frame, one pass.** No push/pop stack, no multi-pass rendering —
  `PLAN.md` §8 reserves draw-list recording for P3.
- **Canvas format is fixed at `R8G8B8A8_UNORM`** for color, matching every other
  texture usage in this framework; no mipmaps; no resize-in-place (destroy and
  recreate is the only path).
- **Depth clear/compare state is a pipeline property, not per-frame global state**
  (unlike CF's `app->clear_depth`/`app->clear_stencil`) — it's fixed at
  `clear_depth = 1.0f`/`clear_stencil = 0` in the render-pass depth target info,
  matching the existing fixed clear-color-per-canvas-vs-swapchain shape rather than
  adding a `bk_gfx_set_clear_depth` with no current consumer.

## 10. Explicitly out of scope

- Stencil test/ops API, MSAA, DPI-aware sizing, multiple canvases per frame,
  canvas resize-in-place, canvas color formats other than `R8G8B8A8_UNORM` (all §9).
- Multi-window, HDR swapchain composition — `PLAN.md` §8 non-goals, unrelated to this
  sub-project's scope.
