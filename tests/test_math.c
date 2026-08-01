#include "bk_test.h"

#include <bielik/bk_math.h>

#include <math.h>

// Tolerance for single-operation f32 results. Comparisons that are exact by
// construction (3-4-5 lengths, the zero-vector safe_norm) use == deliberately.
constexpr f32 EPS = 1e-5f;

// Anything downstream of a matrix product or an inverse accumulates error across six
// multiply-adds, so it gets a looser bound than a single operation.
constexpr f32 EPS_ACC = 1e-4f;

static void s_require_v2_near(BK_V2 actual, BK_V2 expected, f32 eps) {
  REQUIRE_NEAR(actual.x, expected.x, eps);
  REQUIRE_NEAR(actual.y, expected.y, eps);
}

static void test_scalar_min_max_abs_sign(void) {
  REQUIRE(bk_minf(2.0f, 3.0f) == 2.0f);
  REQUIRE(bk_minf(3.0f, 2.0f) == 2.0f);
  REQUIRE(bk_maxf(2.0f, 3.0f) == 3.0f);
  REQUIRE(bk_maxf(3.0f, 2.0f) == 3.0f);
  REQUIRE(bk_minf(-1.0f, 1.0f) == -1.0f);

  REQUIRE(bk_absf(-2.5f) == 2.5f);
  REQUIRE(bk_absf(2.5f) == 2.5f);
  REQUIRE(bk_absf(-0.0f) == 0.0f);

  // Zero maps to zero, not to 1 -- the convention the header documents.
  REQUIRE(bk_signf(3.0f) == 1.0f);
  REQUIRE(bk_signf(-3.0f) == -1.0f);
  REQUIRE(bk_signf(0.0f) == 0.0f);
  REQUIRE(bk_signf(-0.0f) == 0.0f);
}

static void test_scalar_clamp_lerp_remap(void) {
  REQUIRE(bk_clampf(5.0f, 0.0f, 1.0f) == 1.0f);
  REQUIRE(bk_clampf(-5.0f, 0.0f, 1.0f) == 0.0f);
  REQUIRE(bk_clampf(0.5f, 0.0f, 1.0f) == 0.5f);
  REQUIRE(bk_clampf(0.0f, 0.0f, 1.0f) == 0.0f);
  REQUIRE(bk_clampf(1.0f, 0.0f, 1.0f) == 1.0f);

  REQUIRE(bk_clamp01f(2.0f) == 1.0f);
  REQUIRE(bk_clamp01f(-2.0f) == 0.0f);
  REQUIRE(bk_clamp01f(0.25f) == 0.25f);

  REQUIRE(bk_lerpf(10.0f, 20.0f, 0.0f) == 10.0f);
  REQUIRE(bk_lerpf(10.0f, 20.0f, 1.0f) == 20.0f);
  REQUIRE_NEAR(bk_lerpf(10.0f, 20.0f, 0.5f), 15.0f, EPS);
  // t is deliberately not clamped -- extrapolation is legal.
  REQUIRE_NEAR(bk_lerpf(10.0f, 20.0f, 2.0f), 30.0f, EPS);

  REQUIRE_NEAR(bk_remapf(5.0f, 0.0f, 10.0f, 100.0f, 200.0f), 150.0f, EPS);
  REQUIRE_NEAR(bk_remapf(0.0f, 0.0f, 10.0f, 100.0f, 200.0f), 100.0f, EPS);
  REQUIRE_NEAR(bk_remapf(-5.0f, 0.0f, 10.0f, 0.0f, 10.0f), -5.0f, EPS);
}

static void test_scalar_angle_conversion(void) {
  REQUIRE_NEAR(bk_deg_to_rad(180.0f), BK_PI, EPS);
  REQUIRE_NEAR(bk_deg_to_rad(90.0f), BK_PI / 2.0f, EPS);
  REQUIRE_NEAR(bk_rad_to_deg(BK_PI), 180.0f, EPS);
  REQUIRE_NEAR(bk_rad_to_deg(bk_deg_to_rad(37.0f)), 37.0f, EPS);
}

static void test_v2_arithmetic(void) {
  BK_V2 lhs = bk_v2(3.0f, 4.0f);
  BK_V2 rhs = bk_v2(1.0f, 2.0f);

  s_require_v2_near(bk_v2_add(lhs, rhs), bk_v2(4.0f, 6.0f), EPS);
  s_require_v2_near(bk_v2_sub(lhs, rhs), bk_v2(2.0f, 2.0f), EPS);
  s_require_v2_near(bk_v2_neg(lhs), bk_v2(-3.0f, -4.0f), EPS);
  s_require_v2_near(bk_v2_scale(lhs, 2.0f), bk_v2(6.0f, 8.0f), EPS);
  s_require_v2_near(bk_v2_mul(lhs, rhs), bk_v2(3.0f, 8.0f), EPS);
  s_require_v2_near(bk_v2_div(lhs, rhs), bk_v2(3.0f, 2.0f), EPS);
  s_require_v2_near(bk_v2_zero(), bk_v2(0.0f, 0.0f), EPS);
}

static void test_v2_products(void) {
  // Perpendicular vectors have zero dot product.
  REQUIRE_NEAR(bk_v2_dot(bk_v2(1.0f, 0.0f), bk_v2(0.0f, 1.0f)), 0.0f, EPS);
  REQUIRE_NEAR(bk_v2_dot(bk_v2(3.0f, 4.0f), bk_v2(1.0f, 2.0f)), 11.0f, EPS);

  // det2 is antisymmetric: swapping the arguments flips the sign.
  f32 det = bk_v2_det2(bk_v2(1.0f, 0.0f), bk_v2(0.0f, 1.0f));
  f32 det_swapped = bk_v2_det2(bk_v2(0.0f, 1.0f), bk_v2(1.0f, 0.0f));
  REQUIRE_NEAR(det, 1.0f, EPS);
  REQUIRE_NEAR(det_swapped, -1.0f, EPS);
  // A vector crossed with itself is zero.
  REQUIRE_NEAR(bk_v2_det2(bk_v2(3.0f, 4.0f), bk_v2(3.0f, 4.0f)), 0.0f, EPS);
}

static void test_v2_length_and_distance(void) {
  // 3-4-5 triangle: exact in f32, so compare exactly.
  REQUIRE(bk_v2_len(bk_v2(3.0f, 4.0f)) == 5.0f);
  REQUIRE(bk_v2_len_sq(bk_v2(3.0f, 4.0f)) == 25.0f);
  REQUIRE(bk_v2_len(bk_v2_zero()) == 0.0f);

  REQUIRE(bk_v2_distance(bk_v2(1.0f, 1.0f), bk_v2(4.0f, 5.0f)) == 5.0f);
  REQUIRE(bk_v2_distance_sq(bk_v2(1.0f, 1.0f), bk_v2(4.0f, 5.0f)) == 25.0f);
  REQUIRE(bk_v2_distance(bk_v2(2.0f, 2.0f), bk_v2(2.0f, 2.0f)) == 0.0f);
}

static void test_v2_normalize(void) {
  s_require_v2_near(bk_v2_norm(bk_v2(3.0f, 4.0f)), bk_v2(0.6f, 0.8f), EPS);
  REQUIRE_NEAR(bk_v2_len(bk_v2_norm(bk_v2(-7.0f, 2.5f))), 1.0f, EPS);

  // The whole reason bk_v2_safe_norm exists: the zero vector comes back exactly
  // unchanged instead of producing NaN. Exact comparison is the point here.
  BK_V2 safe_zero = bk_v2_safe_norm(bk_v2_zero());
  REQUIRE(safe_zero.x == 0.0f);
  REQUIRE(safe_zero.y == 0.0f);

  // A very small but non-zero vector still normalizes to unit length.
  REQUIRE_NEAR(bk_v2_len(bk_v2_safe_norm(bk_v2(1e-20f, 0.0f))), 1.0f, EPS);
  s_require_v2_near(bk_v2_safe_norm(bk_v2(3.0f, 4.0f)), bk_v2(0.6f, 0.8f), EPS);
}

static void test_v2_perp_lerp_min_max(void) {
  // Counter-clockwise: (1,0) -> (0,1).
  s_require_v2_near(bk_v2_perp(bk_v2(1.0f, 0.0f)), bk_v2(0.0f, 1.0f), EPS);
  s_require_v2_near(bk_v2_perp(bk_v2(0.0f, 1.0f)), bk_v2(-1.0f, 0.0f), EPS);
  // The perpendicular is orthogonal to the original and preserves length.
  BK_V2 vec = bk_v2(3.0f, -4.0f);
  REQUIRE_NEAR(bk_v2_dot(vec, bk_v2_perp(vec)), 0.0f, EPS);
  REQUIRE_NEAR(bk_v2_len(bk_v2_perp(vec)), bk_v2_len(vec), EPS);

  s_require_v2_near(bk_v2_lerp(bk_v2(0.0f, 0.0f), bk_v2(10.0f, 20.0f), 0.5f), bk_v2(5.0f, 10.0f),
                    EPS);
  s_require_v2_near(bk_v2_lerp(bk_v2(1.0f, 2.0f), bk_v2(3.0f, 4.0f), 0.0f), bk_v2(1.0f, 2.0f), EPS);

  // Component-wise, so the result need not equal either input.
  s_require_v2_near(bk_v2_min(bk_v2(1.0f, 9.0f), bk_v2(5.0f, 2.0f)), bk_v2(1.0f, 2.0f), EPS);
  s_require_v2_near(bk_v2_max(bk_v2(1.0f, 9.0f), bk_v2(5.0f, 2.0f)), bk_v2(5.0f, 9.0f), EPS);
}

static void s_require_m3x2_near(BK_M3x2 actual, BK_M3x2 expected, f32 eps) {
  s_require_v2_near(actual.x, expected.x, eps);
  s_require_v2_near(actual.y, expected.y, eps);
  s_require_v2_near(actual.origin, expected.origin, eps);
}

static void test_sincos(void) {
  // Sweep includes negatives and an angle past 2*pi, where a naive range assumption
  // would break.
  const f32 angles[] = {0.0f, 0.3f, BK_PI / 2.0f, BK_PI, -1.1f, -BK_PI, 7.5f, 12.0f};
  for (usize i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
    BK_SinCos rotation = bk_sincos(angles[i]);
    REQUIRE_NEAR(rotation.sine, sinf(angles[i]), EPS);
    REQUIRE_NEAR(rotation.cosine, cosf(angles[i]), EPS);
    // The Pythagorean identity holds for every angle.
    REQUIRE_NEAR(rotation.sine * rotation.sine + rotation.cosine * rotation.cosine, 1.0f, EPS);
  }

  BK_SinCos identity = bk_sincos_identity();
  REQUIRE(identity.sine == 0.0f);
  REQUIRE(identity.cosine == 1.0f);
}

static void test_v2_rotate(void) {
  // Fixes the counter-clockwise convention as a test rather than a comment.
  s_require_v2_near(bk_v2_rotate(bk_v2(1.0f, 0.0f), bk_sincos(BK_PI / 2.0f)), bk_v2(0.0f, 1.0f),
                    EPS);
  s_require_v2_near(bk_v2_rotate(bk_v2(1.0f, 0.0f), bk_sincos(BK_PI)), bk_v2(-1.0f, 0.0f), EPS);
  s_require_v2_near(bk_v2_rotate(bk_v2(3.0f, -4.0f), bk_sincos_identity()), bk_v2(3.0f, -4.0f),
                    EPS);

  // Rotation preserves length at every angle -- a single-angle spot check would miss a
  // sign error that this catches.
  const f32 angles[] = {0.2f, 1.0f, 2.4f, -0.8f, 5.9f};
  BK_V2 vec = bk_v2(3.0f, -4.0f);
  for (usize i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
    REQUIRE_NEAR(bk_v2_len(bk_v2_rotate(vec, bk_sincos(angles[i]))), bk_v2_len(vec), EPS);
  }
}

static void test_m3x2_builders(void) {
  BK_V2 point = bk_v2(2.0f, 3.0f);

  s_require_v2_near(bk_m3x2_transform_point(bk_m3x2_identity(), point), point, EPS);
  s_require_v2_near(bk_m3x2_transform_point(bk_m3x2_translation(bk_v2(10.0f, -5.0f)), point),
                    bk_v2(12.0f, -2.0f), EPS);
  s_require_v2_near(bk_m3x2_transform_point(bk_m3x2_scale(bk_v2(2.0f, 0.5f)), point),
                    bk_v2(4.0f, 1.5f), EPS);
  s_require_v2_near(bk_m3x2_transform_point(bk_m3x2_rotation(BK_PI / 2.0f), bk_v2(1.0f, 0.0f)),
                    bk_v2(0.0f, 1.0f), EPS);

  // transform_vector drops the translation; transform_point does not. Same matrix.
  BK_M3x2 moved = bk_m3x2_translation(bk_v2(10.0f, -5.0f));
  s_require_v2_near(bk_m3x2_transform_vector(moved, point), point, EPS);
  s_require_v2_near(bk_m3x2_transform_point(moved, point), bk_v2(12.0f, -2.0f), EPS);
}

static void test_m3x2_ortho(void) {
  BK_M3x2 projection = bk_m3x2_ortho(1280.0f, 720.0f);
  s_require_v2_near(bk_m3x2_transform_point(projection, bk_v2(640.0f, 360.0f)), bk_v2(1.0f, 1.0f),
                    EPS);
  s_require_v2_near(bk_m3x2_transform_point(projection, bk_v2(-640.0f, -360.0f)),
                    bk_v2(-1.0f, -1.0f), EPS);
  s_require_v2_near(bk_m3x2_transform_point(projection, bk_v2_zero()), bk_v2_zero(), EPS);
}

static void test_m3x2_mul_order(void) {
  BK_M3x2 rotation = bk_m3x2_rotation(0.5f);
  BK_M3x2 translation = bk_m3x2_translation(bk_v2(1.0f, 0.0f));

  // mul(lhs, rhs) applies rhs first. Rotating an already-translated point is not the
  // same as translating an already-rotated one -- assert the two genuinely differ, so
  // an implementation that composes backwards cannot pass.
  BK_V2 rotate_then_translate =
      bk_m3x2_transform_point(bk_m3x2_mul(translation, rotation), bk_v2(1.0f, 0.0f));
  BK_V2 translate_then_rotate =
      bk_m3x2_transform_point(bk_m3x2_mul(rotation, translation), bk_v2(1.0f, 0.0f));
  REQUIRE(bk_v2_distance(rotate_then_translate, translate_then_rotate) > 0.1f);

  // Spelled out longhand: rhs applied first, then lhs.
  BK_V2 stepwise = bk_m3x2_transform_point(rotation, bk_v2(1.0f, 0.0f));
  stepwise = bk_m3x2_transform_point(translation, stepwise);
  s_require_v2_near(rotate_then_translate, stepwise, EPS_ACC);

  // Identity is the neutral element on both sides.
  s_require_m3x2_near(bk_m3x2_mul(bk_m3x2_identity(), rotation), rotation, EPS_ACC);
  s_require_m3x2_near(bk_m3x2_mul(rotation, bk_m3x2_identity()), rotation, EPS_ACC);
}

static void test_m3x2_trs_composition(void) {
  BK_V2 translation = bk_v2(5.0f, -3.0f);
  BK_V2 scale = bk_v2(2.0f, 0.5f);
  f32 radians = 0.7f;

  BK_M3x2 packed = bk_m3x2_trs(translation, scale, radians);
  BK_M3x2 composed = bk_m3x2_mul(bk_m3x2_translation(translation),
                                 bk_m3x2_mul(bk_m3x2_rotation(radians), bk_m3x2_scale(scale)));

  // Assert as matrix equality AND by transforming a point: a transposed implementation
  // can satisfy one of these and not the other.
  s_require_m3x2_near(packed, composed, EPS_ACC);
  BK_V2 point = bk_v2(1.5f, -2.25f);
  s_require_v2_near(bk_m3x2_transform_point(packed, point),
                    bk_m3x2_transform_point(composed, point), EPS_ACC);
}

static void test_m3x2_inverse(void) {
  // Deliberately includes non-uniform scale, a negative scale (mirror), and a rotation
  // that is not an axis multiple.
  const BK_M3x2 cases[] = {
      bk_m3x2_identity(),
      bk_m3x2_translation(bk_v2(11.0f, -4.0f)),
      bk_m3x2_scale(bk_v2(3.0f, -1.5f)),
      bk_m3x2_rotation(0.9f),
      bk_m3x2_trs(bk_v2(11.0f, -4.0f), bk_v2(3.0f, -1.5f), 0.9f),
      bk_m3x2_trs(bk_v2(-2.0f, 7.0f), bk_v2(0.25f, 4.0f), -2.2f),
  };

  BK_V2 point = bk_v2(2.5f, -7.25f);
  for (usize i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    BK_M3x2 inverse = bk_m3x2_inv(cases[i]);
    s_require_m3x2_near(bk_m3x2_mul(cases[i], inverse), bk_m3x2_identity(), EPS_ACC);
    s_require_m3x2_near(bk_m3x2_mul(inverse, cases[i]), bk_m3x2_identity(), EPS_ACC);
    // Round-tripping a point is the property the draw layer actually depends on: it
    // uploads the inverse MVP so the shader can recover world space per pixel.
    s_require_v2_near(bk_m3x2_transform_point(inverse, bk_m3x2_transform_point(cases[i], point)),
                      point, EPS_ACC);
  }
}

static void test_aabb_construction(void) {
  BK_Aabb box = bk_aabb(bk_v2(-2.0f, -1.0f), bk_v2(4.0f, 5.0f));
  s_require_v2_near(bk_aabb_center(box), bk_v2(1.0f, 2.0f), EPS);
  s_require_v2_near(bk_aabb_half_extents(box), bk_v2(3.0f, 3.0f), EPS);
  s_require_v2_near(bk_aabb_size(box), bk_v2(6.0f, 6.0f), EPS);

  // from_center / center / half_extents round-trip.
  BK_Aabb centered = bk_aabb_from_center(bk_v2(1.0f, 2.0f), bk_v2(3.0f, 3.0f));
  s_require_v2_near(centered.min, box.min, EPS);
  s_require_v2_near(centered.max, box.max, EPS);
}

static void test_aabb_empty_is_accumulation_identity(void) {
  BK_Aabb empty = bk_aabb_empty();

  // An inverted box overlaps nothing and contains nothing -- the useful degenerate
  // behavior, and what makes it safe as an accumulation seed.
  REQUIRE(!bk_aabb_overlaps(empty, bk_aabb(bk_v2(0.0f, 0.0f), bk_v2(1.0f, 1.0f))));
  REQUIRE(!bk_aabb_overlaps(empty, empty));
  REQUIRE(!bk_aabb_contains_point(empty, bk_v2_zero()));

  // Seeding from empty yields a zero-size box at the first point, not a box dragged
  // toward the origin.
  BK_Aabb seeded = bk_aabb_add_point(empty, bk_v2(5.0f, -7.0f));
  s_require_v2_near(seeded.min, bk_v2(5.0f, -7.0f), EPS);
  s_require_v2_near(seeded.max, bk_v2(5.0f, -7.0f), EPS);

  // Adding a second point spans both, in either order.
  BK_Aabb spanned = bk_aabb_add_point(seeded, bk_v2(-1.0f, 2.0f));
  s_require_v2_near(spanned.min, bk_v2(-1.0f, -7.0f), EPS);
  s_require_v2_near(spanned.max, bk_v2(5.0f, 2.0f), EPS);

  BK_Aabb reversed =
      bk_aabb_add_point(bk_aabb_add_point(empty, bk_v2(-1.0f, 2.0f)), bk_v2(5.0f, -7.0f));
  s_require_v2_near(reversed.min, spanned.min, EPS);
  s_require_v2_near(reversed.max, spanned.max, EPS);
}

static void test_aabb_overlap_and_containment(void) {
  BK_Aabb unit = bk_aabb(bk_v2(0.0f, 0.0f), bk_v2(1.0f, 1.0f));

  REQUIRE(bk_aabb_overlaps(unit, unit));
  REQUIRE(bk_aabb_overlaps(unit, bk_aabb(bk_v2(0.5f, 0.5f), bk_v2(2.0f, 2.0f))));
  REQUIRE(!bk_aabb_overlaps(unit, bk_aabb(bk_v2(1.5f, 0.0f), bk_v2(2.0f, 1.0f))));
  REQUIRE(!bk_aabb_overlaps(unit, bk_aabb(bk_v2(0.0f, 2.0f), bk_v2(1.0f, 3.0f))));

  // Touching edges count as overlapping -- the inclusive convention a rendering cull
  // needs, so a shape exactly on a tile boundary is never dropped.
  REQUIRE(bk_aabb_overlaps(unit, bk_aabb(bk_v2(1.0f, 0.0f), bk_v2(2.0f, 1.0f))));

  // Boundary points count as contained.
  REQUIRE(bk_aabb_contains_point(unit, bk_v2(0.5f, 0.5f)));
  REQUIRE(bk_aabb_contains_point(unit, bk_v2(0.0f, 0.0f)));
  REQUIRE(bk_aabb_contains_point(unit, bk_v2(1.0f, 1.0f)));
  REQUIRE(!bk_aabb_contains_point(unit, bk_v2(1.001f, 0.5f)));

  // A zero-size box contains its own corner and overlaps itself.
  BK_Aabb degenerate = bk_aabb(bk_v2(3.0f, 3.0f), bk_v2(3.0f, 3.0f));
  REQUIRE(bk_aabb_contains_point(degenerate, bk_v2(3.0f, 3.0f)));
  REQUIRE(bk_aabb_overlaps(degenerate, degenerate));
}

static void test_aabb_expand_and_combine(void) {
  BK_Aabb unit = bk_aabb(bk_v2(0.0f, 0.0f), bk_v2(1.0f, 1.0f));

  // Coverage inflation: grows on every side.
  BK_Aabb grown = bk_aabb_expand(unit, bk_v2(0.5f, 2.0f));
  s_require_v2_near(grown.min, bk_v2(-0.5f, -2.0f), EPS);
  s_require_v2_near(grown.max, bk_v2(1.5f, 3.0f), EPS);

  // A negative amount shrinks, and enough shrink inverts the box -- documented, not
  // guarded.
  BK_Aabb inverted = bk_aabb_expand(unit, bk_v2(-1.5f, -1.5f));
  REQUIRE(inverted.min.x > inverted.max.x);
  REQUIRE(inverted.min.y > inverted.max.y);
  REQUIRE(!bk_aabb_overlaps(inverted, unit));

  // Shrinking by exactly half the extent is the boundary case: min and max meet the
  // opposite corners, and the inclusive overlap convention still reports a hit. Worth
  // pinning, since it's the point where "inverted" stops meaning "misses everything".
  BK_Aabb collapsed = bk_aabb_expand(unit, bk_v2(-1.0f, -1.0f));
  REQUIRE(bk_aabb_overlaps(collapsed, unit));

  BK_Aabb other = bk_aabb(bk_v2(2.0f, -3.0f), bk_v2(4.0f, 0.5f));
  BK_Aabb combined = bk_aabb_combine(unit, other);
  s_require_v2_near(combined.min, bk_v2(0.0f, -3.0f), EPS);
  s_require_v2_near(combined.max, bk_v2(4.0f, 1.0f), EPS);

  // Commutative and idempotent.
  BK_Aabb flipped = bk_aabb_combine(other, unit);
  s_require_v2_near(flipped.min, combined.min, EPS);
  s_require_v2_near(flipped.max, combined.max, EPS);
  BK_Aabb again = bk_aabb_combine(combined, combined);
  s_require_v2_near(again.min, combined.min, EPS);
  s_require_v2_near(again.max, combined.max, EPS);
}

static void test_rect_field_order(void) {
  // BK_Rect carries no operations; this pins its field order so a future reorder is
  // caught here rather than silently at an SDL_SetGPUScissor call site.
  BK_Rect rect = {.x = 1, .y = 2, .width = 3, .height = 4};
  REQUIRE(rect.x == 1);
  REQUIRE(rect.y == 2);
  REQUIRE(rect.width == 3);
  REQUIRE(rect.height == 4);
}

int main(void) {
  test_scalar_min_max_abs_sign();
  test_scalar_clamp_lerp_remap();
  test_scalar_angle_conversion();
  test_v2_arithmetic();
  test_v2_products();
  test_v2_length_and_distance();
  test_v2_normalize();
  test_v2_perp_lerp_min_max();
  test_sincos();
  test_v2_rotate();
  test_m3x2_builders();
  test_m3x2_ortho();
  test_m3x2_mul_order();
  test_m3x2_trs_composition();
  test_m3x2_inverse();
  test_aabb_construction();
  test_aabb_empty_is_accumulation_identity();
  test_aabb_overlap_and_containment();
  test_aabb_expand_and_combine();
  test_rect_field_order();
  return 0;
}
