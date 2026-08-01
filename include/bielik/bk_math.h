#pragma once
#include <bielik/bk_types.h>

#include <math.h>

// Pure 2D math for the draw layer: no SDL, no allocation, no global state. Every
// function is static inline (hot path -- see the design spec's §2) and [[nodiscard]]
// (every function here is pure and total, so a discarded result is always a bug --
// spec §5).

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

/// Linear interpolation from lhs to rhs. t is not clamped, so extrapolation is legal.
[[nodiscard]] static inline f32 bk_lerpf(f32 lhs, f32 rhs, f32 t) {
  return lhs + (rhs - lhs) * t;
}

/// Maps value from [in_lo, in_hi] onto [out_lo, out_hi]. Not clamped; undefined if
/// in_lo == in_hi.
[[nodiscard]] static inline f32 bk_remapf(f32 value, f32 in_lo, f32 in_hi, f32 out_lo, f32 out_hi) {
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

/// Component-wise sum.
[[nodiscard]] static inline BK_V2 bk_v2_add(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x + rhs.x, lhs.y + rhs.y};
}

/// Component-wise difference.
[[nodiscard]] static inline BK_V2 bk_v2_sub(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){lhs.x - rhs.x, lhs.y - rhs.y};
}

/// Negation.
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

/// Dot product.
[[nodiscard]] static inline f32 bk_v2_dot(BK_V2 lhs, BK_V2 rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y;
}

/// The 2D cross product (perp-dot): the signed area of the parallelogram the two
/// vectors span. Its sign gives winding order.
[[nodiscard]] static inline f32 bk_v2_det2(BK_V2 lhs, BK_V2 rhs) {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

/// Squared length. Prefer this over bk_v2_len for comparisons -- no square root.
[[nodiscard]] static inline f32 bk_v2_len_sq(BK_V2 vec) {
  return bk_v2_dot(vec, vec);
}

/// Length.
[[nodiscard]] static inline f32 bk_v2_len(BK_V2 vec) {
  return sqrtf(bk_v2_len_sq(vec));
}

/// Squared distance between two points.
[[nodiscard]] static inline f32 bk_v2_distance_sq(BK_V2 lhs, BK_V2 rhs) {
  return bk_v2_len_sq(bk_v2_sub(lhs, rhs));
}

/// Distance between two points.
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

/// Component-wise minimum -- AABB construction, not vector comparison.
[[nodiscard]] static inline BK_V2 bk_v2_min(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){bk_minf(lhs.x, rhs.x), bk_minf(lhs.y, rhs.y)};
}

/// Component-wise maximum -- AABB construction, not vector comparison.
[[nodiscard]] static inline BK_V2 bk_v2_max(BK_V2 lhs, BK_V2 rhs) {
  return (BK_V2){bk_maxf(lhs.x, rhs.x), bk_maxf(lhs.y, rhs.y)};
}

static_assert(sizeof(BK_V2) == 8, "BK_V2 must be two tightly packed f32");
