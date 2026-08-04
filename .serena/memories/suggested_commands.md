# Bielik2D — Suggested Commands (macOS/Darwin)

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON
cmake --build build
```
Release build for benchmarking/profiling (debug is 5-20x slower — always measure in
release):
```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Test
```
ctest --test-dir build --output-on-failure
```
Run a single test binary directly, e.g.:
```
./build/tests/test_version
```

## Format
```
cmake --build build --target format        # apply .clang-format
cmake --build build --target format-check   # check only, no writes (what CI runs)
```

## Useful CMake options
- `BK_BUILD_SAMPLES` (default ON) — build `samples/`.
- `BK_BUILD_TESTS` (default ON) — build `tests/` + `enable_testing()`.
- `BK_WERROR` — treat warnings as errors (used in the standard dev build above).
- `BK_ASSERT_LEVEL` — override SDL assertion level (0=off,1=release,2=debug,3=paranoid);
  empty (default) = 2 in Debug, 1 otherwise. Don't let SDL infer this.

## Git / worktrees
- Standard git (macOS ships an old git via Xcode CLT; check `git --version` if something
  git-related misbehaves unexpectedly).
- Feature work uses git worktrees under `<repo-root>/.worktrees/<feature>` (gitignored),
  not sibling repos and not directly on `main`.

## System utils (Darwin/BSD — flags differ from GNU)
- `find`, `grep`, `ls` etc. are BSD variants on macOS; e.g. BSD `find` needs `-E` for
  extended regex, BSD `sed -i` needs an explicit (even empty) backup suffix argument.
- `rg` (ripgrep) if installed is a safer default for content search than raw `grep -r`.
