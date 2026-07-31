# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bielik2D is a 2D game framework written in C23, designed to be lightweight, fast, and
easy to use. `PLAN.md` is the normative spec for the public API and module design
(section 6.1 in particular) — read it before changing library headers or adding a
module. `DEVIATIONS.md` records every place implementation deviated from `PLAN.md` or
a task brief, with rationale; check it, and add to it, whenever you knowingly diverge.

Phase 0 (scaffold) and Phase 1 (app core) are implemented: the CMake build system,
`bielik` static library, samples, and test suite described in `PLAN.md` sections 5 and
6 all exist. Check the actual directory contents (`PLAN.md` section 4 has the current
layout) before assuming a command exists beyond what's listed here.

Phase 2 (gfx core) is underway as sub-projects, each with its own spec
(`docs/superpowers/specs/`) and implementation plan (`docs/superpowers/plans/`) —
`PLAN.md` only covers Phase 0/1.

Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`.
Test: `ctest --test-dir build --output-on-failure` (or run a single test
binary directly, e.g. `./build/tests/test_version`).

`THOUGHTS.md` is the working design log for the project and is the primary source of
architectural intent until real code and headers exist. Treat it as a mix of locked
decisions and open questions — the two are not the same, see below.

## Relationship to Cute Framework

Bielik2D is a from-scratch, clean rebuild that treats Cute Framework (CF) as **donor
code**, not a dependency: CF is C++ so a C23 port can't just wrap it, but its zlib/public-
domain license means transliterating its proven pieces (SDF batcher/shaders, draw API
semantics, GLES3 backend) beats reinventing them. A checkout of CF lives at
`/Users/piotr/Work/GitHub/pusewicz/cute_framework` for reference — CF's own `AGENTS.md`
is worth reading as a template for conventions here.

## Architecture decisions already locked in

- **Language/toolchain**: C23, clang-everywhere (clang, clang-cl on Windows, AppleClang,
  Emscripten). MSVC's C23 support lags too far behind to target it; the clang commitment
  also buys uniform `#embed` support.
- **VFS**: PhysFS (same as CF). Route all asset loading through one PhysFS→SDL_IOStream
  bridge; archives-as-mounts double as the data-driven asset-pack mechanism.
- **Draw/SDF layer**: the one module that can't be bought off the shelf — port CF's SDF
  batcher and shaders directly. This is the crown-jewel module (~5.3k lines in CF).
- **Text**: SDL3_ttf's GPU text engine, feeding SDL_GPU directly. Add an MSDF atlas path
  later only if stylized/scalable text is needed. Bake built-in fonts with C23 `#embed`
  instead of vendoring a generated font header.
- **Shaders**: precompile offline with SDL_shadercross (HLSL/SPIR-V → SPIR-V/DXIL/MSL) and
  ship compiled bytecode in asset packs — do not write a runtime transpiler like CF did.
  Current interim toolchain is `glslc`+`spirv-cross` compiling GLSL, not shadercross+HLSL —
  shadercross has no tagged release and no macOS prebuilt DXC; see `DEVIATIONS.md`.
- **Physics/collision**: Box2D v3 (C, SIMD, multithreaded), used via sensors/queries even
  for simple overlap tests. Box3D (same data model/API, released June 2026) is the story
  for optional 3D later, so don't reach for a separate 3D collision lib.
- **Coroutines**: minicoro (same as CF). The Emscripten path runs through fiber/asyncify
  machinery — validate it early since it affects link flags and performance, and interacts
  with the web-backend decision below.
- **glTF**: cgltf, but don't add it until a 3D spike actually starts.
- **ImGui**: dcimgui/cimgui bindings, isolated to one C++ translation unit. The debug-
  tooling value is worth the single C++ TU; don't substitute Nuklear/microui to stay pure C.
- **Networking**: cut entirely. Do not add cute_net/cute_tls/cute_https-equivalents.
- **Asset architecture** (the load-bearing piece — more important than the ECS choice):
  a GUID→path indirection asset database over PhysFS mounts, an offline pipeline (shader
  compile, atlas packing, font baking) that produces one mountable pack, mtime-polling hot
  reload in dev builds, and `#embed` fallbacks for built-ins.
- **SIMD**: write SoA scalar code first and check clang's autovectorization before
  hand-writing SSE/NEON/WASM128 — only where a profiler shows it's needed. Box2D and the
  audio library bring their own SIMD already.
- **Web/graphics backend strategy**: SDL_GPU stays the native backend (already built in
  Phase 1); web gets a second backend built directly against `webgpu.h` via Emscripten's
  `emdawnwebgpu` port (mature and actively maintained as of mid-2026, integrated since
  Emscripten 4.0.10). SDL_GPU's own native WebGPU backend is still experimental (started
  May 2026, no macOS/browser target yet) — not viable today, but the bet is that it
  eventually matures enough to let the two backends converge into one without a rewrite.
  Rejected: replacing SDL_GPU with Dawn-as-the-one-graphics-API everywhere (architecturally
  cleaner, kills the two-backend tax permanently, but means reworking Phase 1's already-
  shipped GPU bring-up and vendoring Dawn as a new heavy native build dependency — too much
  cost for what SDL_GPU already gets natively today). CI gets an Emscripten leg once this
  backend lands.

## Open decisions — do not treat these as settled

- **Audio library.** miniaudio (battle-hardened, zero deps, proven wasm support) vs.
  SDL3_mixer (all-SDL stack coherence, but only months old as a stable release, needs SDL
  3.4.0+). Leaning miniaudio, not decided. MojoAL is ruled out either way (unneeded OpenAL
  compatibility shim).
- **ECS.** flecs v4 (reflection, prefabs, JSON serialization, but a large opinionated
  dependency) vs. pico_ecs (minimal). Explicitly deferred — safe to build against the
  asset layer without picking this yet.

## Planned build order

window/GPU-clear + shader pipeline → sprite batch + atlas → SDF shapes → text →
input/app loop → VFS + asset DB + hot reload → audio → Box2D → coroutines → ImGui tools
→ the Space Delivery port.

v1's finish line is defined as: Space Delivery (the user's existing game) runs on
Bielik2D, feature-identical to its current Cute Framework build. Treat that port as the
scope boundary, not an afterthought — it's what turns this from open-ended framework
infrastructure into a bounded, checkable deliverable.

## Development discipline for this repo

- **Headers-first**: design and agree on the public `.h` API before implementing against
  it. Implementing against a fixed header is the strong case for an LLM; incrementally
  designing an API while implementing it is the weak one.
- One module per session, against a written spec.
- Every module gets a test (pico_unit-style for math/VFS/strings; golden-image tests for
  the renderer — render to texture, hash, compare) and an example app as living
  documentation.
- CI should run on Linux + Windows-clang from day one; add an Emscripten leg once the
  webgpu.h web backend (see the locked-in web/graphics backend decision above) lands.

## SDL_GPU gotchas

- `SDL_CreateGPUDevice` requires `SDL_Init(SDL_INIT_VIDEO)` first, even for a headless
  device with no window (`SDL_GPUSelectBackend` calls `SDL_GetVideoDevice()` internally
  and errors otherwise).
- The swapchain texture is write-only for sampling, but `SDL_DownloadFromGPUTexture`
  works on it directly via a copy pass — no intermediate blit needed for
  screenshot/capture use cases.
- When wrapping GPU-downloaded pixel bytes as an `SDL_Surface`, use SDL's `_32` aliases
  (`SDL_PIXELFORMAT_RGBA32`/`BGRA32`), not the packed `_8888` names — the packed names
  are bit-packed order, not byte-array order, and flip on little-endian.
- `BK_GfxShaderDesc.num_samplers`/`num_uniform_buffers` (and the equivalent compute
  resource counts) must match what the shader binary actually declares. A mismatch
  does **not** fail at `SDL_CreateGPUShader`/pipeline-creation time — it fails
  silently later, at draw/dispatch time, with no `SDL_Log` output and no error
  return: the render pass's whole command buffer is dropped, so a texture that should
  have been cleared-then-drawn instead reads back as all-zero bytes (not even the
  clear color). If a golden-image test's downloaded pixels are unexpectedly all zero
  with no logged failure anywhere in the chain, check the resource counts on every
  shader desc first.
- A texture created with both `SDL_GPU_TEXTUREUSAGE_SAMPLER` and
  `SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE` is valid (confirmed on Metal) — only
  `SAMPLER | GRAPHICS_STORAGE_READ` is documented as an invalid combination. This is
  what makes a compute-shader-filled, later-sampled texture (`BK_GFX_TEXTURE_USAGE_
  COMPUTE_TARGET`) a single texture rather than a render-then-copy-into-a-second-
  texture dance.

## Conventions

Naming:
- Public functions: `bk_` + snake_case (`bk_run`, `bk_frame_alloc`).
- Public types: `BK_` + PascalCase (`BK_AppDesc`, `BK_FrameInfo`).
- Enum values: `BK_` + UPPER_SNAKE (`BK_CONTINUE`).
- Internal linker-visible symbols: `bk__` prefix. File-static functions: `s_` prefix.
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).

C23 usage:
- Use: `bool`/`true`/`false`, `nullptr`, designated initializers, compound
  literals, `constexpr` for constants, `static_assert`, `[[nodiscard]]` on
  functions returning `BK_Result`, `[[maybe_unused]]`, `typeof` where it
  removes duplication.
- Avoid: VLAs (`-Wvla` enforces), `alloca`, `_Generic` unless clearly better,
  `auto` outside obvious initializers, bit-precise ints in public API.
- `#embed` is reserved for later phases (needs Clang 19+/GCC 15); do not use yet.
- Fixed-width numerics: `include/bielik/bk_types.h`'s short aliases (`i8`..`i64`,
  `u8`..`u64`, `f32`/`f64`, `usize`/`isize`, `b32`) are the project convention now —
  prefer them over `stdint.h`'s verbose names in new code.

Style:
- `.clang-format`: LLVM base, 4-space indent, 100 columns,
  `PointerAlignment: Right` (`char *p`), K&R attached braces. Run on everything.
- `.clang-tidy`: `readability-identifier-length` only (min 2 chars, `i`/`x`/`y`/`r`/`g`/`b`/`a`
  exempt) — enforces the no-single-letter-identifiers rule from this sweep. Advisory in CI
  (`continue-on-error: true`) until proven quiet; not yet wired into the default build.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant. Terse; no boilerplate prose.
- Public headers must each compile standalone (enforced by test).
- Errors: no silent failure. Boot-path failures log via `SDL_Log` with a
  `"BK: "` prefix and return `BK_FAIL`. Assertions: `BK_ASSERT` wraps
  `SDL_assert`.
- Includes ordered: own header, then `<bielik/...>`, then SDL, then libc.

Process:
- Never reorganize the file layout beyond section 4 of `PLAN.md`.
- Keep functions small; no premature abstraction; no speculative options.
- Regenerating shader bytecode alone doesn't restage it next to already-built
  binaries unless something forces a relink of the consuming target (tracked in
  bielik2d-c#2) — touch a source file of the sample/test to force it meanwhile.
- Deferred/non-blocking findings (bugs, follow-ups, feature ideas) get filed as
  GitHub issues on `pusewicz/bielik2d-c`, not left as code comments or session notes.
