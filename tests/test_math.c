#include "bk_test.h"

#include <bielik/bk_math.h>

#include <math.h>

// Tolerance for single-operation f32 results. Comparisons that are exact by
// construction (3-4-5 lengths, the zero-vector safe_norm) use == deliberately.
constexpr f32 EPS = 1e-5f;

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

int main(void) {
  test_scalar_min_max_abs_sign();
  test_scalar_clamp_lerp_remap();
  test_scalar_angle_conversion();
  test_v2_arithmetic();
  test_v2_products();
  test_v2_length_and_distance();
  test_v2_normalize();
  test_v2_perp_lerp_min_max();
  return 0;
}
