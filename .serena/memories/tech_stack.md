# Bielik2D — Tech Stack & Locked Architecture Decisions

## Language/toolchain
C23, clang-everywhere (clang, clang-cl on Windows, AppleClang, Emscripten). MSVC is
explicitly rejected by CMakeLists.txt (`FATAL_ERROR` if `CMAKE_C_COMPILER_ID STREQUAL
"MSVC"`) — use clang-cl instead. `#embed` support (needs Clang 19+/GCC 15) is reserved
for later phases, not used yet.

## Build system
CMake ≥3.28. SDL3 fetched via `FetchContent` (currently pinned to release 3.4.8, static
linked). Shader toolchain: `glslc` + `spirv-cross` compiling GLSL → SPIR-V/MSL (interim;
long-term plan is SDL_shadercross HLSL→SPIR-V/DXIL/MSL once it has a tagged release and
macOS DXC prebuilt — see DEVIATIONS.md).

## Locked-in dependencies/decisions (do not re-litigate — see repo CLAUDE.md for full list)
- VFS: PhysFS, via one PhysFS→SDL_IOStream bridge.
- Draw/SDF layer: ported directly from Cute Framework's SDF batcher/shaders (crown-jewel
  module, ~5.3k lines in CF).
- Text: SDL3_ttf GPU text engine → SDL_GPU directly; MSDF atlas only if needed later.
- Physics/collision: Box2D v3 (C, SIMD, multithreaded); Box3D is the story for optional 3D.
- Coroutines: minicoro (same as CF); Emscripten path needs fiber/asyncify validation.
- glTF: cgltf, deferred until a 3D spike starts.
- ImGui: dcimgui/cimgui bindings, isolated to one C++ translation unit.
- Networking: cut entirely (no cute_net/cute_tls/cute_https equivalents).
- Asset architecture: GUID→path indirection DB over PhysFS mounts, offline pipeline
  (shader compile, atlas packing, font baking) → one mountable pack, mtime-polling hot
  reload in dev, `#embed` fallbacks for built-ins.
- SIMD: SoA scalar first, check clang autovectorization before hand SIMD.
- Web/graphics backend: SDL_GPU stays native; web gets a second backend against
  `webgpu.h` via Emscripten's `emdawnwebgpu` port. SDL_GPU's own WebGPU backend is not
  viable yet. Rejected: Dawn-as-the-one-API-everywhere (too costly given Phase 1 already
  shipped on SDL_GPU).

## Open decisions — NOT settled, don't assume an answer
- Audio library: miniaudio (leaning) vs. SDL3_mixer. MojoAL is ruled out either way.
- ECS: flecs v4 vs. pico_ecs. Deferred; asset layer doesn't depend on this choice.

## Testing
pico_unit-style tests (`tests/bk_test.h`, wraps SDL_test's `SDLTest_CompareSurfaces` via
`REQUIRE_SURFACE`; `REQUIRE_PIXEL` for spot-checks where whole-image compare isn't
reproducible, e.g. GPU SDF antialiasing). Every module gets a test + an example app
(`samples/`) as living documentation.
