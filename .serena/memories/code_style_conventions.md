# Bielik2D — Code Style & Conventions

## Naming
- Public functions: `bk_` + snake_case (`bk_run`, `bk_frame_alloc`).
- Public types: `BK_` + PascalCase (`BK_AppDesc`, `BK_FrameInfo`).
- Enum values: `BK_` + UPPER_SNAKE (`BK_CONTINUE`).
- Internal linker-visible symbols: `bk__` prefix. File-static functions: `s_` prefix.
- One module = `include/bielik/bk_<name>.h` + `src/bk_<name>.c` (+ optional
  `src/internal/bk_<name>_internal.h`).

## C23 usage
- Use: `bool`/`true`/`false`, `nullptr`, designated initializers, compound literals,
  `constexpr` for constants, `static_assert`, `[[nodiscard]]` on functions returning
  `BK_Result`, `[[maybe_unused]]`, `typeof` where it removes duplication.
- Avoid: VLAs (`-Wvla` enforced), `alloca`, `_Generic` unless clearly better, `auto`
  outside obvious initializers, bit-precise ints in public API.
- `#embed` reserved for later phases — do not use yet.
- Fixed-width numerics: use `include/bielik/bk_types.h`'s short aliases (`i8`..`i64`,
  `u8`..`u64`, `f32`/`f64`, `usize`/`isize`, `b32`) over `stdint.h`'s verbose names.

## Formatting/linting
- `.clang-format`: LLVM base, 2-space indent, 100 columns, `PointerAlignment: Right`
  (`char *p`), K&R attached braces, `Language: C` (a future ImGui C++ TU needs its own
  `Language: Cpp` section). Apply: `cmake --build build --target format`. Check only:
  `cmake --build build --target format-check` (CI's `format` job runs this and blocks
  the build).
- `.clang-tidy`: only `readability-identifier-length` (min 2 chars; `i`/`x`/`y`/`r`/`g`/
  `b`/`a` exempt). Advisory in CI (`continue-on-error: true`), not wired into default build.
- Includes ordered: quoted/internal headers, then `<bielik/...>`, then `<SDL3/...>`, then
  system headers — enforced by `.clang-format`'s `IncludeBlocks: Regroup`.
- Every public symbol gets a doc comment: one-sentence summary, param notes,
  thread/lifetime notes where relevant. Terse, no boilerplate prose.
- Public headers must each compile standalone (enforced by test, `tests/test_header_*.c`).

## Error handling
- No silent failure. Boot-path failures log via `SDL_Log` with a `"BK: "` prefix and
  return `BK_FAIL`.
- Assertions, three tiers in `bk_app.h` (map 1:1 onto SDL's assertion levels):
  - `BK_ASSERT` → `SDL_assert_release`, **live in Release** (default — most asserts guard
    a pointer the next line dereferences; compiling out trades an abort for UB).
  - `BK_ASSERT_DEBUG` → `SDL_assert`, `BK_ASSERT_PARANOID` → `SDL_assert_paranoid` — for
    checks too expensive to keep once profiling shows it; both start with zero call sites.
  - Level pinned by CMake's `BK_ASSERT_LEVEL` (2 Debug, 1 otherwise) — never let SDL infer
    it (it keys off `__OPTIMIZE__`, never `NDEBUG`).
  - A disabled assert does not evaluate its condition — never put required work inside one.

## Process discipline
- **Check whether SDL already provides it before writing it** — SDL3 is a hard dependency
  already; search `build/_deps/sdl3-src/include/SDL3/` first, and note in the commit or
  header comment what was found and why it was/wasn't used. Deliberate non-use of an SDL
  facility is a DEVIATIONS.md entry (e.g. bk_math's libc `assert`, kept SDL-free on purpose).
- Headers-first: agree the public `.h` API before implementing against it.
- One module per session, against a written spec.
- Never reorganize file layout beyond PLAN.md §4.
- Keep functions small; no premature abstraction; no speculative options.
- Deferred/non-blocking findings (bugs, follow-ups, feature ideas) → filed as GitHub
  issues on `pusewicz/bielik2d-c`, never left as code comments or session notes.

## Working style (from user feedback, applies across sessions)
- TDD red-green; KISS/DRY; atomic commits; human-voice commit messages — no Conventional
  Commits prefixes, no AI signoff/co-author lines the user didn't ask for.
- Keep rolling through multi-phase work without "want me to continue?" check-ins; reserve
  questions for real decisions.
- Always benchmark/profile with `-c release` / `CMAKE_BUILD_TYPE=Release` — debug builds
  are 5-20× slower and are the default for the build/CI/benchmark commands.
- Verify tests by mutation: break the code under test and confirm the test fails, don't
  trust a green run alone as proof the test catches the bug it names.
