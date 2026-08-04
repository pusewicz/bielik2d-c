# Bielik2D — What To Do When a Task Is Complete

1. **Format**: `cmake --build build --target format-check` clean (or run `format` to
   apply, then check).
2. **Build**: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake
   --build build` succeeds with warnings-as-errors on.
3. **Test**: `ctest --test-dir build --output-on-failure` all green.
   - New/changed behavior needs a test: pico_unit-style for math/VFS/strings; for the
     renderer, render-to-texture + `REQUIRE_SURFACE` whole-image compare where the
     expected image is deterministically constructible, `REQUIRE_PIXEL` spot-checks
     otherwise (see DEVIATIONS.md for why `test_draw_gpu.c` differs).
   - Verify new tests by mutation (break the implementation, confirm the test fails)
     before trusting a green run as proof.
   - New public header → gets a `tests/test_header_bk_<name>.c` (standalone-compile check).
4. **New/changed module** → matching example under `samples/` as living documentation.
5. **Deviated from PLAN.md or a task brief?** → add an entry to `DEVIATIONS.md` with
   rationale, in the same session.
6. **Non-blocking bugs/follow-ups/ideas found along the way** → file as a GitHub issue on
   `pusewicz/bielik2d-c`, not a code comment or session note.
7. **Commits**: atomic, human-voice messages (no Conventional Commits prefix, no
   AI-signoff lines unless the user asked for them).
8. Do NOT reorganize the file layout beyond `PLAN.md` §4.

CI mirrors: Linux + Windows-clang build/test, plus the `format` job (blocks the build).
An Emscripten leg is planned once the webgpu.h web backend lands.
