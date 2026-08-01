# `bk_math` Implementation Plan

**Goal:** Add `include/bielik/bk_math.h` — the minimal, draw2d-driven 2D math module
Phase 3 needs — plus its exhaustive test suite, and move `BK_Color` out of `bk_gfx.h`
into it.

**Architecture:** One header-only module (`static inline` throughout, no
`src/bk_math.c`), one test executable, one header-compile stub. Pure: no SDL, no
allocation, no global state. Because the module is header-only, `CMakeLists.txt` at the
repo root needs **no** change — `target_include_directories(bielik PUBLIC include)`
already exports the whole tree. Only `tests/CMakeLists.txt` gains entries.

**Tech Stack:** C23, CMake, clang-everywhere, `<math.h>` (the module's only external
dependency), `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla -Werror`.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-01-bk-math-design.md` — read it first; this
  plan assumes its §2–§9 and does not restate the rationale.
- Every function is `static inline` and `[[nodiscard]]` (spec §5), no exceptions.
- Use `bk_types.h` aliases (`f32`, `i32`, `u32`, `u8`) — never `float`/`int`/`uint32_t`.
- No single-letter identifiers except `i`, `x`, `y`, `r`, `g`, `b`, `a` (`.clang-tidy`
  enforces). This is why `BK_SinCos`'s members are `sine`/`cosine`, not `s`/`c`.
- Epsilons in tests: `1e-5f` for single operations, `1e-4f` for anything downstream of
  a matrix product or inverse. Exact `==` only where exactness is the property under
  test (zero-vector `safe_norm`, hex round-trip, identity).
- Every task builds clean under
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
  and passes `ctest --test-dir build --output-on-failure`.
- Run `cmake --build build --target format-check` before each commit; CI's `format` job
  blocks on it.
- TDD red-green: write the failing test first, watch it fail, then implement. The one
  exception is Task 1's scaffolding step, which has nothing to assert yet.
- Commit messages: human voice, imperative, no Conventional Commits prefixes, no AI
  signoffs. One commit per task.
- Tasks are strictly sequential — each extends the same header and the same test file.

---

## Task 1: Scaffolding, scalars, and `BK_V2`

**Files:** create `include/bielik/bk_math.h`, `tests/test_math.c`,
`tests/test_header_bk_math.c`; modify `tests/CMakeLists.txt`.

- [ ] **Step 1: wire the build.** Add to `tests/CMakeLists.txt`, matching the existing
      pattern exactly:

```cmake
add_executable(test_math test_math.c)
target_link_libraries(test_math PRIVATE bielik bk_warnings)
add_test(NAME test_math COMMAND test_math)

add_library(test_header_bk_math OBJECT test_header_bk_math.c)
target_link_libraries(test_header_bk_math PRIVATE bielik bk_warnings)
```

`tests/test_header_bk_math.c` contains exactly one line:
`#include <bielik/bk_math.h>`. No `target_include_directories(... src)` — this module
has no internals.

- [ ] **Step 2 (red): write the scalar and `BK_V2` tests** in `tests/test_math.c`,
      before the header exists. It must fail to compile — that is the red state.

Cover: `bk_minf`/`bk_maxf`/`bk_absf`/`bk_signf` including negative zero and the
`bk_signf(0) == 0` convention; `bk_clampf` at both bounds and inside;
`bk_clamp01f`; `bk_lerpf` at t=0, t=1, t=0.5, and an unclamped t=2; `bk_remapf`
mapping a known interval; `bk_deg_to_rad`/`bk_rad_to_deg` round-tripping 180 and 90.

For `BK_V2`: construction, `add`/`sub`/`neg`/`scale`/`mul`/`div`; `dot` of
perpendicular vectors is 0; `det2` sign flips with argument order; `len` of (3,4) is
exactly 5; `len_sq` avoids the root; `distance`/`distance_sq`; `norm` yields unit
length; **`bk_v2_safe_norm(bk_v2_zero())` returns exactly zero — `REQUIRE`, not
`REQUIRE_NEAR`** (this assertion is the entire justification for the function
existing); a tiny non-zero vector still normalizes to unit length; `perp` maps (1,0) to
(0,1); `lerp`/`min`/`max` component-wise.

Use `bk_test.h`'s `REQUIRE`, `REQUIRE_NEAR`, `REQUIRE_EQ_U64` — those three are all it
provides. A `static void s_require_v2_near(BK_V2 actual, BK_V2 expected, f32 eps)`
file-static helper keeps the vector assertions readable; `s_` prefix per `CLAUDE.md`.

- [ ] **Step 3 (green): write the header** through `BK_V2`.

```c
#pragma once
#include <bielik/bk_types.h>

#include <math.h>

// Pure 2D math for the draw layer: no SDL, no allocation, no global state. Every
// function is static inline and [[nodiscard]] -- see the design spec's §2 and §5.

/// Ratio of a circle's circumference to its diameter.
constexpr f32 BK_PI = 3.14159265358979323846f;

/// The smaller of two values.
[[nodiscard]] static inline f32 bk_minf(f32 lhs, f32 rhs) {
  return lhs < rhs ? lhs : rhs;
}

/// The larger of two values.
[[nodiscard]] static inline f32 bk_maxf(f32 lhs, f32 rhs) {
  return lhs > rhs ? lhs : rhs;
}

/// Absolute value.
[[nodiscard]] static inline f32 bk_absf(f32 value) {
  return fabsf(value);
}

/// -1, 0, or 1 by the sign of value (0 maps to 0, not 1).
[[nodiscard]] static inline f32 bk_signf(f32 value) {
  if (value > 0.0f) {
    return 1.0f;
  }
  if (value < 0.0f) {
    return -1.0f;
  }
  return 0.0f;
}

/// value constrained to [lo, hi]. Undefined if lo > hi.
[[nodiscard]] static inline f32 bk_clampf(f32 value, f32 lo, f32 hi) {
  return bk_minf(bk_maxf(value, lo), hi);
}

/// value constrained to [0, 1] -- the alpha/interpolation-factor case.
[[nodiscard]] static inline f32 bk_clamp01f(f32 value) {
  return bk_clampf(value, 0.0f, 1.0f);
}

/// Linear interpolation from lhs to rhs. t is not clamped.
[[nodiscard]] static inline f32 bk_lerpf(f32 lhs, f32 rhs, f32 t) {
  return lhs + (rhs - lhs) * t;
}

/// Maps value from [in_lo, in_hi] onto [out_lo, out_hi]. Not clamped; undefined if
/// in_lo == in_hi.
[[nodiscard]] static inline f32 bk_remapf(f32 value, f32 in_lo, f32 in_hi, f32 out_lo,
                                          f32 out_hi) {
  return out_lo + (value - in_lo) / (in_hi - in_lo) * (out_hi - out_lo);
}

/// Degrees to radians.
[[nodiscard]] static inline f32 bk_deg_to_rad(f32 degrees) {
  return degrees * (BK_PI / 180.0f);
}

/// Radians to degrees.
[[nodiscard]] static inline f32 bk_rad_to_deg(f32 radians) {
  return radians * (180.0f / BK_PI);
}

/// A 2D vector: position, direction, size, or uv depending on context.
typedef struct BK_V2 {
  f32 x, y;
} BK_V2;

/// Constructs a vector.
[[nodiscard]] static inline BK_V2 bk_v2(f32 x, f32 y) {
  return (BK_V2){x, y};
}

/// The zero vector.
[[nodiscard]] static inline BK_V2 bk_v2_zero(void) {
  return (BK_V2){0.0f, 0.0f};
}

[[nodiscard]] static inline BK_V2 bk_v2_add(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x + rhs.x, lhs.y + rhs.y};
}

[[nodiscard]] static inline BK_V2 bk_v2_sub(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x - rhs.x, lhs.y - rhs.y};
}

[[nodiscard]] static inline BK_V2 bk_v2_neg(BK_V2 vec) {
  return (BK_V2){-vec.x, -vec.y};
}

/// Scales both components by a scalar.
[[nodiscard]] static inline BK_V2 bk_v2_scale(BK_V2 vec, f32 factor) {
  return (BK_V2){vec.x * factor, vec.y * factor};
}

/// Component-wise multiply -- the non-uniform-scale case, not a dot or cross product.
[[nodiscard]] static inline BK_V2 bk_v2_mul(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x * rhs.x, lhs.y * rhs.y};
}

/// Component-wise divide. Undefined if either component of rhs is zero.
[[nodiscard]] static inline BK_V2 bk_v2_div(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x / rhs.x, lhs.y / rhs.y};
}

[[nodiscard]] static inline f32 bk_v2_dot(BK_V2 lhs, BK_V2 rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y;
}

/// The 2D cross product (perp-dot). Signed area of the parallelogram; its sign gives
/// winding order.
[[nodiscard]] static inline f32 bk_v2_det2(BK_V2 lhs, BK_V2 rhs) {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

/// Squared length. Prefer this over bk_v2_len for comparisons -- no square root.
[[nodiscard]] static inline f32 bk_v2_len_sq(BK_V2 vec) {
  return bk_v2_dot(vec, vec);
}

[[nodiscard]] static inline f32 bk_v2_len(BK_V2 vec) {
  return sqrtf(bk_v2_len_sq(vec));
}

[[nodiscard]] static inline f32 bk_v2_distance_sq(BK_V2 lhs, BK_V2 rhs) {
  return bk_v2_len_sq(bk_v2_sub(lhs, rhs));
}

[[nodiscard]] static inline f32 bk_v2_distance(BK_V2 lhs, BK_V2 rhs) {
  return bk_v2_len(bk_v2_sub(lhs, rhs));
}

/// Unit vector in the same direction. Undefined for the zero vector -- use
/// bk_v2_safe_norm when the input can be degenerate.
[[nodiscard]] static inline BK_V2 bk_v2_norm(BK_V2 vec) {
  return bk_v2_scale(vec, 1.0f / bk_v2_len(vec));
}

/// Unit vector in the same direction, returning the zero vector unchanged instead of
/// dividing by zero. Reach for this on user-supplied geometry -- a zero-length segment
/// is a legal thing for a game to draw.
[[nodiscard]] static inline BK_V2 bk_v2_safe_norm(BK_V2 vec) {
  f32 len_sq = bk_v2_len_sq(vec);
  if (len_sq == 0.0f) {
    return bk_v2_zero();
  }
  return bk_v2_scale(vec, 1.0f / sqrtf(len_sq));
}

/// Rotated 90 degrees counter-clockwise: (-y, x). Negate for the clockwise perp.
[[nodiscard]] static inline BK_V2 bk_v2_perp(BK_V2 vec) {
  return (BK_V2){-vec.y, vec.x};
}

/// Component-wise linear interpolation. t is not clamped.
[[nodiscard]] static inline BK_V2 bk_v2_lerp(BK_V2 lhs, BK_V2 rhs, f32 t) {
  return (BK_V2){bk_lerpf(lhs.x, rhs.x, t), bk_lerpf(lhs.y, rhs.y, t)};
}

/// Component-wise minimum/maximum -- AABB construction, not vector comparison.
[[nodiscard]] static inline BK_V2 bk_v2_min(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){bk_minf(lhs.x, rhs.x), bk_minf(lhs.y, rhs.y)};
}

[[nodiscard]] static inline BK_V2 bk_v2_max(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){bk_maxf(lhs.x, rhs.x), bk_maxf(lhs.y, rhs.y)};
}

static_assert(sizeof(BK_V2) == 8, "BK_V2 must be two tightly packed f32");
```

Note `bk_v2_safe_norm` computes `len_sq` once and takes its own `sqrtf` rather than
calling `bk_v2_norm` — one square root, one branch, no redundant length computation.

**Commit:** `add bk_math.h with scalar and vector primitives`

---

## Task 2: `BK_SinCos` and `BK_M3x2`

**Files:** modify `include/bielik/bk_math.h`, `tests/test_math.c`.

- [ ] **Step 1 (red): write the rotation and transform tests.**

Cover the properties the spec's §7 names, not just spot checks:

- `bk_sincos(theta).sine`/`.cosine` match `sinf`/`cosf` within `1e-5f` across a sweep
  including negative angles and values past `2*BK_PI`; `sine^2 + cosine^2` is 1 within
  `1e-5f`.
- `bk_v2_rotate(bk_v2(1,0), bk_sincos(BK_PI/2))` is `(0,1)` within `1e-5f` — this
  fixes the counter-clockwise convention as a test rather than a comment.
- **Rotation preserves length**: `bk_v2_len(bk_v2_rotate(vec, rot))` equals
  `bk_v2_len(vec)` within `1e-5f` across a spread of angles. A single-angle spot check
  misses sign errors; this does not.
- **Inverse round-trip**: `bk_m3x2_mul(m, bk_m3x2_inv(m))` is identity within `1e-4f`
  over transforms including non-uniform scale, negative scale (a mirror), and a
  non-axis-aligned rotation. Separately,
  `transform_point(inv(m), transform_point(m, p)) == p` within `1e-4f`.
- **TRS composition order**: `bk_m3x2_trs(t, s, r)` equals
  `mul(translation(t), mul(rotation(r), scale(s)))` — scale innermost. Assert both as
  matrix equality *and* by transforming a point through both paths, because a
  transposed implementation can satisfy one and not the other.
- `transform_vector` ignores translation while `transform_point` does not — assert on
  the same matrix with a non-zero origin.
- `bk_m3x2_ortho(width, height)` maps `(width/2, height/2)` to `(1, 1)` and the origin
  to `(0, 0)`.
- `bk_m3x2_mul` is non-commutative: build two transforms where `mul(a,b)` and
  `mul(b,a)` differ, and assert they do. This catches an implementation that composes
  in the wrong order but passes every symmetric test.

- [ ] **Step 2 (green): implement.** Append to the header:

```c
/// A rotation carried as its sine and cosine rather than an angle, so rotating N
/// points costs no trigonometry per point.
typedef struct BK_SinCos {
  f32 sine, cosine;
} BK_SinCos;

/// Computes the sine/cosine pair for an angle in radians.
[[nodiscard]] static inline BK_SinCos bk_sincos(f32 radians) {
  return (BK_SinCos){sinf(radians), cosf(radians)};
}

/// The identity rotation.
[[nodiscard]] static inline BK_SinCos bk_sincos_identity(void) {
  return (BK_SinCos){0.0f, 1.0f};
}

/// Rotates a vector by a rotation.
[[nodiscard]] static inline BK_V2 bk_v2_rotate(BK_V2 vec, BK_SinCos rotation) {
  return (BK_V2){vec.x * rotation.cosine - vec.y * rotation.sine,
                 vec.x * rotation.sine + vec.y * rotation.cosine};
}

/// A 2D affine transform stored as the images of the basis vectors plus a translation.
/// Transforming a point p yields x * p.x + y * p.y + origin.
typedef struct BK_M3x2 {
  BK_V2 x;      // image of (1, 0)
  BK_V2 y;      // image of (0, 1)
  BK_V2 origin; // translation
} BK_M3x2;

/// The identity transform.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_identity(void) {
  return (BK_M3x2){{1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}};
}

/// Pure translation.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_translation(BK_V2 translation) {
  return (BK_M3x2){{1.0f, 0.0f}, {0.0f, 1.0f}, translation};
}

/// Pure (possibly non-uniform) scale about the origin.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_scale(BK_V2 scale) {
  return (BK_M3x2){{scale.x, 0.0f}, {0.0f, scale.y}, {0.0f, 0.0f}};
}

/// Pure rotation about the origin, counter-clockwise, in radians.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_rotation(f32 radians) {
  BK_SinCos rotation = bk_sincos(radians);
  return (BK_M3x2){{rotation.cosine, rotation.sine},
                   {-rotation.sine, rotation.cosine},
                   {0.0f, 0.0f}};
}

/// Applies the full transform, translation included -- for positions.
[[nodiscard]] static inline BK_V2 bk_m3x2_transform_point(BK_M3x2 transform, BK_V2 point) {
  return (BK_V2){transform.x.x * point.x + transform.y.x * point.y + transform.origin.x,
                 transform.x.y * point.x + transform.y.y * point.y + transform.origin.y};
}

/// Applies the linear part only, skipping translation -- for directions, normals, and
/// extents.
[[nodiscard]] static inline BK_V2 bk_m3x2_transform_vector(BK_M3x2 transform, BK_V2 vec) {
  return (BK_V2){transform.x.x * vec.x + transform.y.x * vec.y,
                 transform.x.y * vec.x + transform.y.y * vec.y};
}

/// Scale, then rotate, then translate -- the sprite transform, composed once instead
/// of by three bk_m3x2_mul calls.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_trs(BK_V2 translation, BK_V2 scale, f32 radians) {
  BK_SinCos rotation = bk_sincos(radians);
  return (BK_M3x2){{rotation.cosine * scale.x, rotation.sine * scale.x},
                   {-rotation.sine * scale.y, rotation.cosine * scale.y},
                   translation};
}

/// An orthographic projection mapping a width x height box centered on the origin onto
/// normalized device coordinates ([-1, 1] on both axes, y up).
[[nodiscard]] static inline BK_M3x2 bk_m3x2_ortho(f32 width, f32 height) {
  return (BK_M3x2){{2.0f / width, 0.0f}, {0.0f, 2.0f / height}, {0.0f, 0.0f}};
}

/// Composition: the transform applying rhs first, then lhs.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_mul(BK_M3x2 lhs, BK_M3x2 rhs) {
  return (BK_M3x2){bk_m3x2_transform_vector(lhs, rhs.x),
                   bk_m3x2_transform_vector(lhs, rhs.y),
                   bk_m3x2_transform_point(lhs, rhs.origin)};
}

/// The inverse transform. Undefined for a singular transform (zero determinant, e.g. a
/// zero scale on either axis). The draw layer uploads this into the GPU payload so the
/// fragment shader can recover world space per pixel for SDF evaluation, which is why
/// it is core API rather than a convenience.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_inv(BK_M3x2 transform) {
  f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
  f32 inv_det = 1.0f / det;
  BK_M3x2 result = {{transform.y.y * inv_det, -transform.x.y * inv_det},
                    {-transform.y.x * inv_det, transform.x.x * inv_det},
                    {0.0f, 0.0f}};
  result.origin = bk_v2_neg(bk_m3x2_transform_vector(result, transform.origin));
  return result;
}

static_assert(sizeof(BK_M3x2) == 24, "BK_M3x2 must be six tightly packed f32");
```

`bk_m3x2_inv` builds the inverted linear part first, then reuses
`bk_m3x2_transform_vector` on it to compute the inverted translation — that is why
`result.origin` is assigned in a second statement rather than in the initializer.

**Commit:** `add 2D rotations and affine transforms to bk_math`

---

## Task 3: `BK_Aabb` and `BK_Rect`

**Files:** modify `include/bielik/bk_math.h`, `tests/test_math.c`.

- [ ] **Step 1 (red): write the AABB tests.** Beyond construction and accessors:

- An inverted box from `bk_aabb_empty()` overlaps nothing and contains no point.
- `bk_aabb_add_point` starting from `bk_aabb_empty()` produces a zero-size box at that
  point; adding a second point produces the box spanning both, in either order.
- A zero-size box contains its own corner and overlaps itself — the inclusive
  touching-edges convention, asserted rather than assumed. Also assert two boxes
  sharing exactly one edge *do* overlap.
- `bk_aabb_expand` by a negative amount shrinks, and enough negative shrink inverts the
  box (documented behavior, not guarded).
- `bk_aabb_combine` is commutative and idempotent.
- `bk_aabb_from_center` / `bk_aabb_center` / `bk_aabb_half_extents` round-trip.

`bk_aabb_empty` needs `INFINITY` from `<math.h>`, already included.

`BK_Rect` has no operations, so it gets no runtime test — the header stub compiling is
its whole contract. Do assert its field order is usable in a designated initializer in
one line of `test_math.c`, so a future field reorder is caught.

- [ ] **Step 2 (green): implement.** Append:

```c
/// An axis-aligned bounding box. min <= max on both axes is the caller's invariant;
/// nothing here enforces it, and an inverted box reports no overlap and contains no
/// point, which is the useful degenerate behavior.
typedef struct BK_Aabb {
  BK_V2 min, max;
} BK_Aabb;

/// Constructs a box from its corners.
[[nodiscard]] static inline BK_Aabb bk_aabb(BK_V2 min, BK_V2 max) {
  return (BK_Aabb){min, max};
}

/// Constructs a box from its center and half-extents.
[[nodiscard]] static inline BK_Aabb bk_aabb_from_center(BK_V2 center, BK_V2 half_extents) {
  return (BK_Aabb){bk_v2_sub(center, half_extents), bk_v2_add(center, half_extents)};
}

/// An inverted box (min at +inf, max at -inf) -- the identity for bk_aabb_add_point,
/// so bounds accumulation starts here.
[[nodiscard]] static inline BK_Aabb bk_aabb_empty(void) {
  return (BK_Aabb){{INFINITY, INFINITY}, {-INFINITY, -INFINITY}};
}

[[nodiscard]] static inline BK_V2 bk_aabb_center(BK_Aabb box) {
  return bk_v2_scale(bk_v2_add(box.min, box.max), 0.5f);
}

/// Half the box's width and height.
[[nodiscard]] static inline BK_V2 bk_aabb_half_extents(BK_Aabb box) {
  return bk_v2_scale(bk_v2_sub(box.max, box.min), 0.5f);
}

/// The box's full width and height.
[[nodiscard]] static inline BK_V2 bk_aabb_size(BK_Aabb box) {
  return bk_v2_sub(box.max, box.min);
}

/// Grows the box by amount on every side. The draw layer's coverage inflation
/// (radius + stroke + antialias band) is exactly this.
[[nodiscard]] static inline BK_Aabb bk_aabb_expand(BK_Aabb box, BK_V2 amount) {
  return (BK_Aabb){bk_v2_sub(box.min, amount), bk_v2_add(box.max, amount)};
}

/// The box grown to contain point. Accumulates polygon and path bounds.
[[nodiscard]] static inline BK_Aabb bk_aabb_add_point(BK_Aabb box, BK_V2 point) {
  return (BK_Aabb){bk_v2_min(box.min, point), bk_v2_max(box.max, point)};
}

/// The smallest box containing both inputs.
[[nodiscard]] static inline BK_Aabb bk_aabb_combine(BK_Aabb lhs, BK_Aabb rhs) {
  return (BK_Aabb){bk_v2_min(lhs.min, rhs.min), bk_v2_max(lhs.max, rhs.max)};
}

/// Whether two boxes intersect. Touching edges count as overlapping.
[[nodiscard]] static inline bool bk_aabb_overlaps(BK_Aabb lhs, BK_Aabb rhs) {
  return lhs.min.x <= rhs.max.x && rhs.min.x <= lhs.max.x && lhs.min.y <= rhs.max.y &&
         rhs.min.y <= lhs.max.y;
}

/// Whether the box contains point. Points on the boundary count as contained.
[[nodiscard]] static inline bool bk_aabb_contains_point(BK_Aabb box, BK_V2 point) {
  return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y &&
         point.y <= box.max.y;
}

/// An integer pixel rectangle. Carries no operations: it exists so P3.2's scissor and
/// viewport API has one type instead of four loose ints, and both are handed to
/// SDL_GPU field by field.
typedef struct BK_Rect {
  i32 x, y, width, height;
} BK_Rect;
```

**Commit:** `add axis-aligned bounding boxes and a pixel rect to bk_math`

---

## Task 4: move `BK_Color` and add color operations

**Files:** modify `include/bielik/bk_math.h`, `include/bielik/bk_gfx.h`,
`tests/test_math.c`, `DEVIATIONS.md`.

This is the only task touching a file outside the new module. Per spec §3, every
existing `BK_Color` use site already includes `bk_gfx.h`, so adding
`#include <bielik/bk_math.h>` to `bk_gfx.h` and deleting the local definition is
source-compatible with no other edits. **Verify that rather than trusting it**: after
the move, a full `cmake --build build` with `-DBK_WERROR=ON` must be clean, and
`grep -rn "BK_Color" include src samples tests` must show no file that fails to reach
the definition.

- [ ] **Step 1 (red): write the color tests.**

- `bk_color_to_rgba8(bk_color_hex(hex)) == hex` via `REQUIRE_EQ_U64` for a set
  including `0x00000000`, `0xFFFFFFFF`, and an asymmetric value like `0x12345678` —
  asymmetric matters, because a channel-order bug round-trips a palindrome cleanly.
- `bk_color_rgba8(255, 128, 0, 255)` produces the expected floats within `1e-5f`.
- Out-of-range channels clamp rather than wrap: `bk_color_to_rgba8` of a color with
  `r = 2.0f` and `g = -1.0f` yields `0xFF` and `0x00`, not garbage.
- `bk_color_premultiply` at alpha 0 is all zeros with alpha 0; at alpha 1 is the
  identity; at alpha 0.5 halves rgb and leaves alpha alone.
- `bk_color_lerp` at t=0 and t=1, alpha included.
- `bk_color_white`/`black`/`clear` have the expected components.

- [ ] **Step 2 (green): append to `bk_math.h`:**

```c
/// RGBA color, one float per channel, nominally [0, 1] -- values above 1 are legal and
/// survive to the GPU, since the draw layer's command format carries color as halves.
typedef struct BK_Color {
  f32 r, g, b, a;
} BK_Color;

[[nodiscard]] static inline BK_Color bk_color(f32 r, f32 g, f32 b, f32 a) {
  return (BK_Color){r, g, b, a};
}

/// From four 8-bit channels.
[[nodiscard]] static inline BK_Color bk_color_rgba8(u8 r, u8 g, u8 b, u8 a) {
  return (BK_Color){(f32)r / 255.0f, (f32)g / 255.0f, (f32)b / 255.0f, (f32)a / 255.0f};
}

/// From 0xRRGGBBAA.
[[nodiscard]] static inline BK_Color bk_color_hex(u32 hex) {
  return bk_color_rgba8((u8)((hex >> 24) & 0xFFu), (u8)((hex >> 16) & 0xFFu),
                        (u8)((hex >> 8) & 0xFFu), (u8)(hex & 0xFFu));
}

/// To 0xRRGGBBAA, clamping each channel to [0, 1] and rounding.
[[nodiscard]] static inline u32 bk_color_to_rgba8(BK_Color color) {
  u32 r = (u32)(bk_clamp01f(color.r) * 255.0f + 0.5f);
  u32 g = (u32)(bk_clamp01f(color.g) * 255.0f + 0.5f);
  u32 b = (u32)(bk_clamp01f(color.b) * 255.0f + 0.5f);
  u32 a = (u32)(bk_clamp01f(color.a) * 255.0f + 0.5f);
  return (r << 24) | (g << 16) | (b << 8) | a;
}

/// Multiplies rgb by a. The draw layer's command format stores premultiplied color, so
/// this runs once per recorded command.
[[nodiscard]] static inline BK_Color bk_color_premultiply(BK_Color color) {
  return (BK_Color){color.r * color.a, color.g * color.a, color.b * color.a, color.a};
}

/// Component-wise linear interpolation, alpha included. t is not clamped.
[[nodiscard]] static inline BK_Color bk_color_lerp(BK_Color lhs, BK_Color rhs, f32 t) {
  return (BK_Color){bk_lerpf(lhs.r, rhs.r, t), bk_lerpf(lhs.g, rhs.g, t),
                    bk_lerpf(lhs.b, rhs.b, t), bk_lerpf(lhs.a, rhs.a, t)};
}

[[nodiscard]] static inline BK_Color bk_color_white(void) {
  return (BK_Color){1.0f, 1.0f, 1.0f, 1.0f};
}

[[nodiscard]] static inline BK_Color bk_color_black(void) {
  return (BK_Color){0.0f, 0.0f, 0.0f, 1.0f};
}

/// Transparent black -- the additive/accumulation identity, not "no color".
[[nodiscard]] static inline BK_Color bk_color_clear(void) {
  return (BK_Color){0.0f, 0.0f, 0.0f, 0.0f};
}

static_assert(sizeof(BK_Color) == 16, "BK_Color must be four tightly packed f32");
```

- [ ] **Step 3: edit `include/bielik/bk_gfx.h`.** Delete the `BK_Color` struct
      definition and its doc comment; add `#include <bielik/bk_math.h>` alongside the
      existing `#include <bielik/bk_types.h>`. Leave everything else untouched.

- [ ] **Step 4: add two `DEVIATIONS.md` entries**, following the file's existing
      one-paragraph-with-rationale style:

  1. **`bk_math` ships header-only, no `src/bk_math.c`** — deviates from `CLAUDE.md`'s
     "One module = header + `.c`" rule. Cite the hot-path rationale and the two
     empirically verified properties from spec §2 (unused `static inline` stays clean
     under `-Werror`; file-scope `constexpr` in a header gets internal linkage, one
     copy per TU, so it does not collide at link time).
  2. **`BK_Color` moved from `bk_gfx.h` to `bk_math.h`** — `PLAN.md` §6.1 names
     `bk_gfx.h` as its home. Cite the verified source compatibility (all 17 use sites
     already include `bk_gfx.h`) and the reason the type belongs with its operations.

**Commit:** `move BK_Color into bk_math and add color operations`

---

## Final verification checklist

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBK_WERROR=ON && cmake --build build`
      is clean — no warnings, ASan/UBSan enabled.
- [ ] `ctest --test-dir build --output-on-failure` — all tests pass, including the 14
      pre-existing ones (nothing regressed from the `BK_Color` move).
- [ ] `cmake --build build --target format-check` passes.
- [ ] A Release build also compiles: `cmake -S . -B build-release
      -DCMAKE_BUILD_TYPE=Release -DBK_WERROR=ON && cmake --build build-release`.
      `static_assert`s and `[[nodiscard]]` behave the same, but this catches anything
      that only compiled because of Debug-only settings.
- [ ] `grep -rn "float \|double \|uint32_t\|int32_t" include/bielik/bk_math.h` returns
      nothing — the module uses `bk_types.h` aliases exclusively.
- [ ] Every public symbol in `bk_math.h` has a doc comment.
- [ ] `DEVIATIONS.md` has both new entries.
- [ ] Mutation check on the strongest assertion: temporarily flip a sign in
      `bk_m3x2_inv` (e.g. `-transform.x.y` to `transform.x.y`), confirm
      `test_math` **fails**, then revert. A test suite for pure math that passes a
      deliberately broken inverse is not testing anything.
