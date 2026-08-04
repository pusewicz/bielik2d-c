# Copilot code review instructions for Bielik2D

<!-- Review-focused distillation of CLAUDE.md and .claude/agents/code-reviewer.md.
     Those files are canonical; when conventions change there, update this file too. -->

## Project context

Bielik2D is a 2D game framework written in C23 on SDL3/SDL_GPU: a static library
(`include/bielik/` + `src/`), sample apps (`samples/`), and an SDL_test-based test suite
(`tests/`). It compiles with clang everywhere — clang on Linux, AppleClang on macOS,
clang-cl on Windows, Emscripten later — so C23 features are fair game but MSVC-isms and
POSIX-isms are not.

## Do not comment on

CI and tooling already enforce these; comments about them are noise:

- Formatting, whitespace, brace style, include ordering — a blocking clang-format CI job
  covers all of it.
- Compiler warnings — the build runs with `-Werror` on all three platforms in both
  Release and Debug.
- Short identifier names — clang-tidy's `readability-identifier-length` covers this.
- Committed generated shader artifacts (`shaders/*.spv`, `shaders/*.msl`) — checked in by
  design; the GLSL next to them is the source.
- `BK_ASSERT` being active in Release builds — intentional: most asserts guard a pointer
  the next line dereferences, so compiling them out trades an abort for undefined
  behavior.
- The internal `bk_atlas` module having no callers — by design until the sprite
  sub-project wires it up.

## Review priorities, most important first

### 1. Error-path correctness

Project policy is no silent failure. Every failure must log (`SDL_Log` with a `"BK: "`
prefix on boot paths) and return visibly (`BK_FAIL` or `nullptr`). Flag:

- Swallowed or ignored error returns, especially `[[nodiscard]]` `BK_Result` values.
- Cleanup paths that leak on partial initialization.
- Destroy/free functions that are not safe no-ops on `nullptr`.
- Required work placed inside an assert — disabled asserts do not evaluate their
  condition.

### 2. Public API discipline

- A change to a public header (`include/bielik/`) or to documented behavior that
  diverges from the module's design doc should come with a `DEVIATIONS.md` entry; ask
  about it if missing.
- Every new public symbol needs a `///` doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant.
- Every new public header needs a standalone-compile test TU
  (`tests/test_header_bk_<name>.c`) and a `tests/CMakeLists.txt` entry.

### 3. Cross-platform hazards

- POSIX-only calls in portable code; clang-cl/Windows landmines.
- Endianness: packed `SDL_PIXELFORMAT_*8888` formats where the `_32` byte-order aliases
  (`SDL_PIXELFORMAT_RGBA32`/`BGRA32`) belong — the packed names flip on little-endian.
- Assumptions a future Emscripten/WebGPU build will break.

### 4. SDL_GPU pitfalls

- Shader-desc resource counts (`num_samplers`, `num_uniform_buffers`, compute
  equivalents) must match what the shader binary declares. A mismatch fails silently at
  draw time — the command buffer is dropped with nothing logged, and readbacks come back
  all-zero.
- No hardcoded depth-stencil formats; probe via `bk_gfx_depth_stencil_format` (Metal has
  no D24S8).
- No `SDL_BlitGPUTexture`/copy while a render pass is open on the involved texture.
- `SDL_Init(SDL_INIT_VIDEO)` must precede `SDL_CreateGPUDevice`, even headless.

### 5. C23 policy

Flag: VLAs, `alloca`, `auto` outside obvious initializers, bit-precise ints (`_BitInt`)
in public API, `NULL` where `nullptr` belongs, and verbose `stdint.h` names
(`uint32_t`…) in new code where the `bk_types.h` aliases (`i8`–`i64`, `u8`–`u64`,
`f32`/`f64`, `usize`/`isize`, `b32`) are the convention.

### 6. Naming

Public functions `bk_` + snake_case; public types `BK_` + PascalCase; enum values `BK_`
+ UPPER_SNAKE; internal linker-visible symbols `bk__`; file-static functions `s_`.

### 7. Test adequacy

- New behavior needs tests, error paths included — not just happy paths.
- Renderer tests: prefer a whole-image `REQUIRE_SURFACE` compare against a
  CPU-constructed expected surface where the expected image is deterministic;
  `REQUIRE_PIXEL` spot-checks only where it is not (e.g. SDF antialiasing). No committed
  golden-image files.
- Never call `SDLTest_Assert` directly — it is swallowed at assert level ≤ 1; use the
  macros in `tests/bk_test.h`.

### 8. Prefer SDL3

SDL3 is a hard dependency; flag hand-rolled facilities (string/path/endian helpers,
timers, assertion plumbing, etc.) that SDL3 already ships.

## Comment style

Concrete findings only: point at the line, say what breaks and under what scenario, and
suggest a direction. Skip praise and diff restatement. If you are unsure whether
something is a real defect, say so explicitly rather than asserting it.
