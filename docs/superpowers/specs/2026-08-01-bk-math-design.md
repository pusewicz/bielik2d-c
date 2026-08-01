# Bielik2D — Phase 3, Sub-project 1: `bk_math`

## 0. Context and scope

There is no math module in this repository. No `bk_math.h`, no `BK_V2`, no `BK_M3x2`,
no transform of any kind. `PLAN.md` §4's directory layout does not list one and §7's
roadmap (P2–P12) never introduces one. This is a genuine gap in `PLAN.md` rather than a
deliberate deferral: Phase 0/1 needed no geometry (a clock, a task shim, and a
clear-colored swapchain), and Phase 2's gfx core got by with caller-supplied NDC
coordinates baked into vertex buffers by hand — `samples/04_textured_quad/main.c`
literally hardcodes its quad in clip space. Phase 3 cannot do that. Every draw2d API
takes positions, sizes, rotations, and a camera.

Phase 3 targets a port of **current** Cute Framework's tiled-compute command renderer
(`src/cute_draw.cpp`, `src/internal/cute_draw_internal.h`, `tools/builtin_shaders.h`).
This sub-project builds only the math that renderer consumes.

Scope is deliberately **minimal and draw2d-driven**, not a port of CF's 3503-line
`cute_math.h`. Every type and function below is here because a named Phase 3 consumer
needs it; §8 lists what was left out and where it would come from if it is ever
actually needed. `cute_math.h` is donor reference material (zlib), not a dependency.

This module is **pure**: no SDL, no allocation, no global state. It is the one module in
the repo whose tests need neither a GPU nor a display server, which makes it the
strongest candidate for real property-based coverage (§7).

## 1. Module boundaries and file layout

```
bielik2d/
  include/bielik/
    bk_math.h                    (new)
    bk_gfx.h                     (modified: BK_Color moves out, includes bk_math.h)
  tests/
    test_math.c                  (new)
    test_header_bk_math.c        (new)
    CMakeLists.txt               (modified: two entries)
```

No `src/bk_math.c` — see §2. The root `CMakeLists.txt` needs **no** change:
`target_include_directories(bielik PUBLIC include)` already exports the whole `include/`
tree, and a header-only module adds no translation unit to
`add_library(bielik STATIC ...)`.

## 2. Decision: header-only `static inline`

`CLAUDE.md` states the module rule as "One module = `include/bielik/bk_<name>.h` +
`src/bk_<name>.c` (+ optional `src/internal/bk_<name>_internal.h`)". `bk_math` ships as
a header only, with every function `static inline`. This is a deviation and gets a
`DEVIATIONS.md` entry.

Rationale: every function in this module is between one and eight arithmetic
operations, and the Phase 3 consumer calls them in per-sprite, per-vertex loops — the
batch transform path is the hot loop of the entire framework. Forcing them through a
translation-unit boundary would mean either accepting the call overhead or depending on
LTO to undo it. `CLAUDE.md` also commits the project to checking clang's
autovectorization on SoA scalar code before reaching for SIMD; that only works if the
arithmetic is visible to the compiler at the call site.

Checked for exceptions: the largest function is `bk_m3x2_inv`, which is a 2×2
determinant, a reciprocal, four multiplies, and a transformed translation — still well
inside inlining range. Nothing here justifies a `.c` file, and creating one holding a
single function would be worse than the deviation.

Two properties this depends on were **verified empirically**, not assumed, by compiling
a two-translation-unit probe with the project's exact flags
(`-std=c23 -Wno-everything -Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla -Werror`,
AppleClang 21.0.0):

1. `static inline` in a header does not trip `-Wunused-function` when a including TU
   uses none of it — the probe carried a deliberately unused function and compiled
   clean under `-Werror`.
2. A file-scope `constexpr f32` in a header included by two TUs links without a
   duplicate-symbol error. Taking its address in both TUs yielded *different* pointers,
   which confirms C23 `constexpr` objects get internal linkage (one copy per TU, like
   `static const`) rather than external. So `BK_PI` can be a `constexpr` in the header,
   as `CLAUDE.md` prefers for constants, instead of falling back to a macro.

`tests/test_header_bk_math.c` still enforces standalone compilation exactly as every
other public header does.

## 3. Decision: `BK_Color` moves from `bk_gfx.h` to `bk_math.h`

`BK_Color` is currently defined in `include/bielik/bk_gfx.h`. It moves here, and
`bk_gfx.h` gains `#include <bielik/bk_math.h>`.

This is source-compatible, verified rather than assumed. `grep -rn "BK_Color" include
src samples tests` returns 17 hits across `bk_gfx.h`, `src/bk_gfx.c`,
`src/internal/bk_gfx_internal.h`, `samples/01_clear`, `samples/02_ticks`,
`tests/test_gfx.c`, and `tests/test_gfx_capture.c` — **every one of them already
includes `bk_gfx.h`**, directly or through `bk_gfx_internal.h`. The type name does not
change, so no use site needs editing.

The move is right on the merits, not just convenient: color arithmetic (premultiply,
lerp, hex conversion) is math, and the Phase 3 command format packs premultiplied color
as two `packHalf2x16` words, so those operations run per-command in the batcher rather
than in the gfx layer. Leaving the type in `bk_gfx.h` while its operations live in
`bk_math.h` would be the worse split. Gets a `DEVIATIONS.md` entry, since `PLAN.md`
§6.1 names `bk_gfx.h` as `BK_Color`'s home.

## 4. Decision: layout is guaranteed, not incidental

`BK_V2`, `BK_M3x2`, and `BK_Color` are `memcpy`'d into GPU-visible buffers by later
sub-projects — the command stream uploads AABBs and the inverse MVP as raw float
payload. Their sizes are therefore load-bearing, and the header asserts them, following
the precedent `bk_types.h` sets with its `sizeof` assertions:

```c
static_assert(sizeof(BK_V2) == 8, "BK_V2 must be two tightly packed f32");
static_assert(sizeof(BK_M3x2) == 24, "BK_M3x2 must be six tightly packed f32");
static_assert(sizeof(BK_Color) == 16, "BK_Color must be four tightly packed f32");
```

`BK_M3x2` is deliberately **flat** (three `BK_V2` members: the images of the two basis
vectors plus the translation) rather than CF's nested `CF_M2x2 m; CF_V2 p;`. Flat means
one fewer public type, and the six floats map onto the GPU payload's two `vec4` slots
(six used, two padding) without a nested-struct offset argument.

`BK_Rect` gets no assertion: it is a CPU-side scissor/viewport descriptor that P3.2
hands to `SDL_SetGPUScissor`/`SDL_SetGPUViewport` field by field, never uploaded.

## 5. Decision: `[[nodiscard]]` applies to every function here

`CLAUDE.md` says "`[[nodiscard]]` on functions returning `BK_Result`". This module
widens that to every function it declares. Every function here is pure and total —
calling one and discarding the result is unconditionally a bug, and there is no
`-W` flag that catches a discarded struct return. This is a widening of `CLAUDE.md`'s
rule in the same spirit, not a contradiction of it, so it needs no `DEVIATIONS.md`
entry; the rule is stated here so the implementer applies it uniformly instead of
case by case.

## 6. Public API — `include/bielik/bk_math.h`

```c
#pragma once
#include <bielik/bk_types.h>

#include <math.h>

// Pure 2D math for the draw layer: no SDL, no allocation, no global state. Every
// function is static inline (see the design spec's §2) and [[nodiscard]] (§5).

/// Ratio of a circle's circumference to its diameter.
constexpr f32 BK_PI = 3.14159265358979323846f;

//--------------------------------------------------------------------------------
// Scalars
//
// f32-only. C23's _Generic could dispatch on f64 too, but CLAUDE.md discourages it
// and nothing in the draw layer computes in double.

/// The smaller of two values.
[[nodiscard]] static inline f32 bk_minf(f32 lhs, f32 rhs) { return lhs < rhs ? lhs : rhs; }

/// The larger of two values.
[[nodiscard]] static inline f32 bk_maxf(f32 lhs, f32 rhs) { return lhs > rhs ? lhs : rhs; }

/// Absolute value.
[[nodiscard]] static inline f32 bk_absf(f32 value);

/// -1, 0, or 1 by the sign of value (0 maps to 0, not 1).
[[nodiscard]] static inline f32 bk_signf(f32 value);

/// value constrained to [lo, hi]. Undefined if lo > hi.
[[nodiscard]] static inline f32 bk_clampf(f32 value, f32 lo, f32 hi);

/// value constrained to [0, 1] -- the alpha/interpolation-factor case.
[[nodiscard]] static inline f32 bk_clamp01f(f32 value);

/// Linear interpolation from lhs to rhs. t is not clamped.
[[nodiscard]] static inline f32 bk_lerpf(f32 lhs, f32 rhs, f32 t);

/// Maps value from [in_lo, in_hi] onto [out_lo, out_hi]. Not clamped; undefined if
/// in_lo == in_hi.
[[nodiscard]] static inline f32 bk_remapf(f32 value, f32 in_lo, f32 in_hi, f32 out_lo,
                                          f32 out_hi);

/// Degrees to radians.
[[nodiscard]] static inline f32 bk_deg_to_rad(f32 degrees);

/// Radians to degrees.
[[nodiscard]] static inline f32 bk_rad_to_deg(f32 radians);

//--------------------------------------------------------------------------------
// BK_V2

/// A 2D vector: position, direction, size, or uv depending on context.
typedef struct BK_V2 {
  f32 x, y;
} BK_V2;

/// Constructs a vector. Exists so call sites read bk_v2(3, 4) rather than repeating
/// the compound-literal spelling.
[[nodiscard]] static inline BK_V2 bk_v2(f32 x, f32 y);

/// The zero vector.
[[nodiscard]] static inline BK_V2 bk_v2_zero(void);

[[nodiscard]] static inline BK_V2 bk_v2_add(BK_V2 lhs, BK_V2 rhs);
[[nodiscard]] static inline BK_V2 bk_v2_sub(BK_V2 lhs, BK_V2 rhs);
[[nodiscard]] static inline BK_V2 bk_v2_neg(BK_V2 vec);

/// Scales both components by a scalar.
[[nodiscard]] static inline BK_V2 bk_v2_scale(BK_V2 vec, f32 factor);

/// Component-wise multiply -- the non-uniform-scale case, not a dot or cross product.
[[nodiscard]] static inline BK_V2 bk_v2_mul(BK_V2 lhs, BK_V2 rhs);

/// Component-wise divide. Undefined if either component of rhs is zero.
[[nodiscard]] static inline BK_V2 bk_v2_div(BK_V2 lhs, BK_V2 rhs);

[[nodiscard]] static inline f32 bk_v2_dot(BK_V2 lhs, BK_V2 rhs);

/// The 2D cross product (perp-dot): lhs.x * rhs.y - lhs.y * rhs.x. Signed area of the
/// parallelogram; its sign gives winding order.
[[nodiscard]] static inline f32 bk_v2_det2(BK_V2 lhs, BK_V2 rhs);

/// Squared length. Prefer this over bk_v2_len for comparisons -- no square root.
[[nodiscard]] static inline f32 bk_v2_len_sq(BK_V2 vec);

[[nodiscard]] static inline f32 bk_v2_len(BK_V2 vec);

[[nodiscard]] static inline f32 bk_v2_distance_sq(BK_V2 lhs, BK_V2 rhs);
[[nodiscard]] static inline f32 bk_v2_distance(BK_V2 lhs, BK_V2 rhs);

/// Unit vector in the same direction. Undefined for the zero vector -- use
/// bk_v2_safe_norm when the input can be degenerate.
[[nodiscard]] static inline BK_V2 bk_v2_norm(BK_V2 vec);

/// Unit vector in the same direction, returning the zero vector unchanged instead of
/// dividing by zero. This is the one to reach for on user-supplied geometry (a
/// zero-length segment is a legal thing for a game to draw).
[[nodiscard]] static inline BK_V2 bk_v2_safe_norm(BK_V2 vec);

/// Rotated 90 degrees counter-clockwise: (-y, x). Negate for the clockwise perp.
[[nodiscard]] static inline BK_V2 bk_v2_perp(BK_V2 vec);

/// Component-wise linear interpolation. t is not clamped.
[[nodiscard]] static inline BK_V2 bk_v2_lerp(BK_V2 lhs, BK_V2 rhs, f32 t);

/// Component-wise minimum/maximum -- AABB construction, not vector comparison.
[[nodiscard]] static inline BK_V2 bk_v2_min(BK_V2 lhs, BK_V2 rhs);
[[nodiscard]] static inline BK_V2 bk_v2_max(BK_V2 lhs, BK_V2 rhs);

//--------------------------------------------------------------------------------
// BK_SinCos

/// A rotation carried as its sine and cosine rather than an angle, so rotating N
/// points costs no trigonometry per point. Members are named in full because
/// .clang-tidy's readability-identifier-length requires >= 2 characters, and because
/// `sin`/`cos` risk colliding with math.h's names.
typedef struct BK_SinCos {
  f32 sine, cosine;
} BK_SinCos;

/// Computes the sine/cosine pair for an angle in radians.
[[nodiscard]] static inline BK_SinCos bk_sincos(f32 radians);

/// The identity rotation (sine 0, cosine 1).
[[nodiscard]] static inline BK_SinCos bk_sincos_identity(void);

/// Rotates a vector by a rotation.
[[nodiscard]] static inline BK_V2 bk_v2_rotate(BK_V2 vec, BK_SinCos rotation);

//--------------------------------------------------------------------------------
// BK_M3x2
//
// A 2D affine transform stored as the images of the basis vectors plus a translation
// -- six floats, flat (see the design spec's §4). Transforming a point p yields
// x * p.x + y * p.y + origin.

/// A 2D affine transform: rotation/scale/shear plus translation.
typedef struct BK_M3x2 {
  BK_V2 x;      // image of (1, 0)
  BK_V2 y;      // image of (0, 1)
  BK_V2 origin; // translation
} BK_M3x2;

/// The identity transform.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_identity(void);

/// Pure translation.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_translation(BK_V2 translation);

/// Pure (possibly non-uniform) scale about the origin.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_scale(BK_V2 scale);

/// Pure rotation about the origin, counter-clockwise, in radians.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_rotation(f32 radians);

/// Scale, then rotate, then translate -- the sprite transform, composed once instead
/// of by three bk_m3x2_mul calls.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_trs(BK_V2 translation, BK_V2 scale, f32 radians);

/// An orthographic projection mapping a width x height box centered on the origin onto
/// normalized device coordinates ([-1, 1] on both axes, y up). Composed with the
/// inverse of a camera transform to build the draw layer's MVP.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_ortho(f32 width, f32 height);

/// Composition: the transform applying rhs first, then lhs.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_mul(BK_M3x2 lhs, BK_M3x2 rhs);

/// The inverse transform. Undefined for a singular transform (zero determinant, e.g.
/// a zero scale on either axis). The draw layer uploads this into the GPU payload so
/// the fragment shader can recover world space per pixel for SDF evaluation, which is
/// why it is core API rather than a convenience.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_inv(BK_M3x2 transform);

/// Applies the full transform, translation included -- for positions.
[[nodiscard]] static inline BK_V2 bk_m3x2_transform_point(BK_M3x2 transform, BK_V2 point);

/// Applies the linear part only, skipping translation -- for directions, normals, and
/// extents.
[[nodiscard]] static inline BK_V2 bk_m3x2_transform_vector(BK_M3x2 transform, BK_V2 vec);

//--------------------------------------------------------------------------------
// BK_Aabb

/// An axis-aligned bounding box. min <= max on both axes is the caller's invariant;
/// nothing here enforces it, and an inverted box reports no overlap and contains no
/// point, which is the useful degenerate behavior.
typedef struct BK_Aabb {
  BK_V2 min, max;
} BK_Aabb;

/// Constructs a box from its corners.
[[nodiscard]] static inline BK_Aabb bk_aabb(BK_V2 min, BK_V2 max);

/// Constructs a box from its center and half-extents.
[[nodiscard]] static inline BK_Aabb bk_aabb_from_center(BK_V2 center, BK_V2 half_extents);

/// An inverted box (min at +inf, max at -inf) -- the identity for bk_aabb_add_point,
/// so bounds accumulation starts here.
[[nodiscard]] static inline BK_Aabb bk_aabb_empty(void);

[[nodiscard]] static inline BK_V2 bk_aabb_center(BK_Aabb box);

/// Half the box's width and height.
[[nodiscard]] static inline BK_V2 bk_aabb_half_extents(BK_Aabb box);

/// The box's full width and height.
[[nodiscard]] static inline BK_V2 bk_aabb_size(BK_Aabb box);

/// Grows the box by amount on every side. The draw layer's coverage inflation
/// (radius + stroke + antialias band) is exactly this.
[[nodiscard]] static inline BK_Aabb bk_aabb_expand(BK_Aabb box, BK_V2 amount);

/// The box grown to contain point. Accumulates polygon and path bounds.
[[nodiscard]] static inline BK_Aabb bk_aabb_add_point(BK_Aabb box, BK_V2 point);

/// The smallest box containing both inputs.
[[nodiscard]] static inline BK_Aabb bk_aabb_combine(BK_Aabb lhs, BK_Aabb rhs);

/// Whether two boxes intersect. Touching edges count as overlapping.
[[nodiscard]] static inline bool bk_aabb_overlaps(BK_Aabb lhs, BK_Aabb rhs);

/// Whether the box contains point. Points on the boundary count as contained.
[[nodiscard]] static inline bool bk_aabb_contains_point(BK_Aabb box, BK_V2 point);

//--------------------------------------------------------------------------------
// BK_Rect

/// An integer pixel rectangle. Carries no operations: it exists so P3.2's scissor and
/// viewport API has one type instead of four loose ints, and both are handed to SDL_GPU
/// field by field.
typedef struct BK_Rect {
  i32 x, y, width, height;
} BK_Rect;

//--------------------------------------------------------------------------------
// BK_Color
//
// Moved here from bk_gfx.h (see the design spec's §3). bk_gfx.h includes this header,
// so every existing use site keeps compiling unchanged.

/// RGBA color, one float per channel, nominally [0, 1] -- values above 1 are legal and
/// survive to the GPU, since the draw layer's command format carries color as halves.
typedef struct BK_Color {
  f32 r, g, b, a;
} BK_Color;

[[nodiscard]] static inline BK_Color bk_color(f32 r, f32 g, f32 b, f32 a);

/// From 0xRRGGBBAA.
[[nodiscard]] static inline BK_Color bk_color_hex(u32 hex);

/// From four 8-bit channels.
[[nodiscard]] static inline BK_Color bk_color_rgba8(u8 r, u8 g, u8 b, u8 a);

/// To 0xRRGGBBAA, rounding each channel and clamping to [0, 1] first.
[[nodiscard]] static inline u32 bk_color_to_rgba8(BK_Color color);

/// Multiplies rgb by a. The draw layer's command format stores premultiplied color, so
/// this runs once per recorded command.
[[nodiscard]] static inline BK_Color bk_color_premultiply(BK_Color color);

/// Component-wise linear interpolation, alpha included. t is not clamped.
[[nodiscard]] static inline BK_Color bk_color_lerp(BK_Color lhs, BK_Color rhs, f32 t);

[[nodiscard]] static inline BK_Color bk_color_white(void);
[[nodiscard]] static inline BK_Color bk_color_black(void);

/// Transparent black -- the additive/accumulation identity, not "no color".
[[nodiscard]] static inline BK_Color bk_color_clear(void);

static_assert(sizeof(BK_V2) == 8, "BK_V2 must be two tightly packed f32");
static_assert(sizeof(BK_M3x2) == 24, "BK_M3x2 must be six tightly packed f32");
static_assert(sizeof(BK_Color) == 16, "BK_Color must be four tightly packed f32");
```

Bodies are elided above for readability; the implementation plan carries them. They are
all one to eight arithmetic operations, and `math.h` supplies `sqrtf`, `sinf`, `cosf`,
and `fabsf` — the module's only external dependency.

## 7. Testing

`tests/test_math.c`, using the existing `tests/bk_test.h` harness, which provides
exactly `REQUIRE`, `REQUIRE_EQ_U64`, and `REQUIRE_NEAR(a, b, eps)`. No SDL init, no GPU,
no display server — this test runs everywhere, including the CI legs where the
GPU-dependent tests are `continue-on-error`.

Epsilons, stated once and used consistently: **`1e-5f`** for single-operation results
(the f32 round-off floor for values near 1.0), **`1e-4f`** for anything downstream of a
matrix product or an inverse, where error accumulates across six multiply-adds. Do not
compare f32 with `==` anywhere except the exact-zero and identity cases named below,
where exactness is the actual property under test.

Properties worth asserting, beyond per-function spot checks:

- **Inverse round-trip.** `bk_m3x2_mul(m, bk_m3x2_inv(m))` is the identity within
  `1e-4f`, checked over a spread of transforms including non-uniform scale, negative
  scale (a mirror), and a rotation that is not an axis multiple. Separately,
  `bk_m3x2_transform_point(bk_m3x2_inv(m), bk_m3x2_transform_point(m, p)) == p`.
- **TRS composition order.** `bk_m3x2_trs(t, s, r)` must equal
  `mul(translation(t), mul(rotation(r), scale(s)))` — scale innermost. Assert it as
  matrix equality *and* by transforming a point through both, because a transposed
  implementation can satisfy one and not the other.
- **Degenerate normalize.** `bk_v2_safe_norm(bk_v2_zero())` returns exactly the zero
  vector — `REQUIRE`, not `REQUIRE_NEAR`. This is the entire reason the function is
  separate from `bk_v2_norm`, so it is the assertion that justifies the API. Also check
  a very small but non-zero vector still normalizes to unit length.
- **`BK_SinCos` round-trip.** `bk_sincos(theta).sine`/`.cosine` match `sinf`/`cosf`
  within `1e-5f` across a sweep including negatives and values past 2*pi, and
  `sine^2 + cosine^2 == 1` within `1e-5f`. `bk_v2_rotate` by `bk_sincos(pi/2)` maps
  (1,0) to (0,1) within `1e-5f`, fixing the counter-clockwise convention as a test, not
  just a comment.
- **Rotation preserves length.** `bk_v2_len(bk_v2_rotate(v, r)) == bk_v2_len(v)` within
  `1e-5f` for a spread of angles — catches a sign error that a single-angle spot check
  would miss.
- **Degenerate AABBs.** An inverted box (from `bk_aabb_empty`) overlaps nothing and
  contains no point. `bk_aabb_add_point` starting from `bk_aabb_empty` produces a
  zero-size box at that point. A zero-size box contains its own corner and overlaps
  itself — the touching-edges convention, asserted rather than assumed.
- **AABB expand/combine algebra.** `expand` by a negative amount shrinks and can invert
  (documented, not guarded); `combine` is commutative and idempotent.
- **Color round-trip.** `bk_color_to_rgba8(bk_color_hex(h)) == h` for a set of hex
  values including `0x00000000` and `0xFFFFFFFF`, using `REQUIRE_EQ_U64`. Out-of-range
  channels clamp rather than wrap. `bk_color_premultiply` at alpha 0 yields all zeros
  and at alpha 1 is the identity.
- **Layout.** The `static_assert`s in §4 are compile-time, so `test_header_bk_math.c`
  compiling *is* that test. No runtime assertion needed.

`tests/test_header_bk_math.c` contains only `#include <bielik/bk_math.h>`, registered as
an OBJECT library like the other eight header stubs.

## 8. Explicitly out of scope

Each of these is omitted because no Phase 3 consumer needs it, not because it would be
wrong to have. Where it would come from later:

- **3D: `BK_V3`, `BK_M4x4`, quaternions.** `PLAN.md` treats 3D as a later option and
  `CLAUDE.md` names Box3D as the physics story for it. Nothing in draw2d is 3D.
- **Rays, halfspaces, raycasts, and collision queries** (GJK, overlap tests, distance
  queries). `CLAUDE.md` locks Box2D v3 for physics and collision in P8, used via
  sensors and queries *even for simple overlap tests* — writing them here would be
  building the thing that decision exists to avoid. `bk_aabb_overlaps` is the exception
  and earns its place as a rendering cull, not a collision test.
- **Easing, splines, curves.** No consumer. CF's Bezier work lives in its curve-text
  and path features, which are P5/later.
- **Noise and RNG.** `cute_noise.h` is 2414 lines of CF; nothing in P3 is procedural.
- **`BK_Transform` (position + rotation, no scale).** CF has one; the draw layer's
  sprite path needs scale, so `BK_M3x2` covers it. Add if a consumer wants the smaller
  form.
- **f64 or `_Generic` overloads.** `CLAUDE.md` discourages `_Generic` unless clearly
  better, and nothing in the draw layer computes in double.
- **SIMD.** `CLAUDE.md` is explicit: write SoA scalar code, check clang's
  autovectorization, hand-write intrinsics only where a profiler demands it. §2's
  header-only decision is what keeps that door open.

## 9. Decisions and rationale (do not relitigate in implementation sessions)

- **Header-only `static inline`, no `src/bk_math.c`** (§2) — hot path, deviation
  recorded in `DEVIATIONS.md`.
- **`BK_Color` moves out of `bk_gfx.h`** (§3) — verified source-compatible, deviation
  recorded in `DEVIATIONS.md`.
- **`BK_M3x2` is flat, six floats, basis-images-plus-origin** rather than CF's nested
  `M2x2 + V2` (§4) — one fewer public type, and it maps onto the GPU payload's two
  `vec4` slots without nested offsets.
- **Layout `static_assert`s on the GPU-uploaded types** (§4) — these get `memcpy`'d
  into command payloads later; a silent padding change would corrupt the stream in a
  way that surfaces as garbled pixels, not a compile error.
- **`[[nodiscard]]` on everything** (§5) — every function is pure and total; a
  discarded result is unconditionally a bug and nothing else catches it.
- **`bk_v2_norm` and `bk_v2_safe_norm` are both public.** CF makes the same split. The
  unchecked one is what the batcher calls on geometry it constructed itself; the safe
  one is for user-supplied input, where a zero-length segment is legal to draw. Merging
  them would tax the hot path; dropping the safe one would put a divide-by-zero in the
  public API's path.
- **`bk_aabb_empty` returns an inverted box, not a zero box.** It is the identity for
  `bk_aabb_add_point`; a zero box at the origin would wrongly drag every accumulated
  bound toward (0, 0).
- **Touching edges count as overlapping, boundary points count as contained.** Stated
  and tested rather than left to the implementation, because rendering culls want the
  inclusive convention (a shape exactly on a tile edge must not be dropped).
- **`BK_Rect` has no operations.** It is a P3.2 scissor/viewport descriptor handed to
  SDL field by field. Adding intersect/contains helpers with no caller is the
  speculative option `CLAUDE.md` forbids.
- **`math.h`, not SDL's math wrappers.** This module is deliberately SDL-free so its
  tests need no SDL init at all; `CLAUDE.md`'s SDL-everything rule is about
  *allocation*, and this module allocates nothing.
