# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bielik2D is a 2D game framework written in C23, designed to be lightweight, fast, and
easy to use. The repository is currently in the planning stage: `src/`, `include/`, and
`vendor/` are empty, and `CMakeLists.txt` is a stub. There is no build, lint, or test
tooling yet — check the actual directory contents before assuming a command exists, and
update this file with real commands (build, single-test invocation, etc.) as soon as the
build system lands.

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

## Open decisions — do not treat these as settled

- **Web/graphics backend strategy.** This gates a lot of downstream work (Emscripten CI,
  whether a GLES3 backend gets built at all) and needs to be decided before committing to
  it, not inferred by an agent:
  (a) CF's approach — SDL_GPU + a second GLES3 backend for web, plus a shader permutation
  matrix and a permanent two-backend testing tax;
  (b) native-first — SDL_GPU only, structured so a second backend could be added, web
  deferred until SDL ships WebGPU support (no ETA);
  (c) sokol_gfx instead of SDL_GPU — the only single-API route to web today, but it drops
  the SDL_GPU requirement.
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
- CI should run on Linux + Windows-clang (+ Emscripten, only if web option (a) above is
  chosen) from day one.
