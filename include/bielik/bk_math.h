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

/// A rotation carried as its sine and cosine rather than an angle, so rotating N points
/// costs no trigonometry per point. Members are spelled out because .clang-tidy's
/// readability-identifier-length requires at least two characters, and because sin/cos
/// risk colliding with math.h's names.
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

/// Rotates a vector counter-clockwise by a rotation.
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
  return (BK_M3x2){
      {1.0f, 0.0f},
      {0.0f, 1.0f},
      {0.0f, 0.0f}
  };
}

/// Pure translation.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_translation(BK_V2 translation) {
  return (BK_M3x2){
      {1.0f, 0.0f},
      {0.0f, 1.0f},
      translation
  };
}

/// Pure (possibly non-uniform) scale about the origin.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_scale(BK_V2 scale) {
  return (BK_M3x2){
      {scale.x, 0.0f   },
      {0.0f,    scale.y},
      {0.0f,    0.0f   }
  };
}

/// Pure rotation about the origin, counter-clockwise, in radians.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_rotation(f32 radians) {
  BK_SinCos rotation = bk_sincos(radians);
  return (BK_M3x2){
      {rotation.cosine, rotation.sine  },
      {-rotation.sine,  rotation.cosine},
      {0.0f,            0.0f           }
  };
}

/// Scale, then rotate, then translate -- the sprite transform, composed once instead of
/// by three bk_m3x2_mul calls.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_trs(BK_V2 translation, BK_V2 scale, f32 radians) {
  BK_SinCos rotation = bk_sincos(radians);
  return (BK_M3x2){
      {rotation.cosine * scale.x, rotation.sine * scale.x  },
      {-rotation.sine * scale.y,  rotation.cosine * scale.y},
      translation
  };
}

/// An orthographic projection mapping a width x height box centered on the origin onto
/// normalized device coordinates ([-1, 1] on both axes, y up). Composed with the inverse
/// of a camera transform to build the draw layer's MVP.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_ortho(f32 width, f32 height) {
  return (BK_M3x2){
      {2.0f / width, 0.0f         },
      {0.0f,         2.0f / height},
      {0.0f,         0.0f         }
  };
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

/// Composition: the transform applying rhs first, then lhs.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_mul(BK_M3x2 lhs, BK_M3x2 rhs) {
  return (BK_M3x2){bk_m3x2_transform_vector(lhs, rhs.x), bk_m3x2_transform_vector(lhs, rhs.y),
                   bk_m3x2_transform_point(lhs, rhs.origin)};
}

/// The inverse transform. Undefined for a singular transform (zero determinant, e.g. a
/// zero scale on either axis). The draw layer uploads this into the GPU payload so the
/// fragment shader can recover world space per pixel for SDF evaluation, which is why
/// it is core API rather than a convenience.
[[nodiscard]] static inline BK_M3x2 bk_m3x2_inv(BK_M3x2 transform) {
  f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
  f32 inv_det = 1.0f / det;
  BK_M3x2 result = {
      {transform.y.y * inv_det,  -transform.x.y * inv_det},
      {-transform.y.x * inv_det, transform.x.x * inv_det },
      {0.0f,                     0.0f                    }
  };
  result.origin = bk_v2_neg(bk_m3x2_transform_vector(result, transform.origin));
  return result;
}

static_assert(sizeof(BK_M3x2) == 24, "BK_M3x2 must be six tightly packed f32");

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

/// An inverted box (min at +inf, max at -inf) -- the identity for bk_aabb_add_point, so
/// bounds accumulation starts here. A zero box at the origin would instead drag every
/// accumulated bound toward (0, 0). Only add_point/combine/overlaps/contains_point are
/// meaningful on a box that hasn't accumulated anything yet: center reads back NaN and
/// size -inf, since there are no bounds to describe.
[[nodiscard]] static inline BK_Aabb bk_aabb_empty(void) {
  return (BK_Aabb){
      {INFINITY,  INFINITY },
      {-INFINITY, -INFINITY}
  };
}

/// The box's midpoint.
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

/// Grows the box by amount on every side. The draw layer's coverage inflation (radius +
/// stroke + antialias band) is exactly this. A negative amount shrinks, and enough
/// shrink inverts the box.
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

/// Whether two boxes intersect. Touching edges count as overlapping -- the inclusive
/// convention a rendering cull needs, so a shape exactly on a tile boundary is never
/// dropped.
[[nodiscard]] static inline bool bk_aabb_overlaps(BK_Aabb lhs, BK_Aabb rhs) {
  return lhs.min.x <= rhs.max.x && rhs.min.x <= lhs.max.x && lhs.min.y <= rhs.max.y &&
         rhs.min.y <= lhs.max.y;
}

/// Whether the box contains point. Points on the boundary count as contained.
[[nodiscard]] static inline bool bk_aabb_contains_point(BK_Aabb box, BK_V2 point) {
  return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y &&
         point.y <= box.max.y;
}

/// An integer pixel rectangle. Carries no operations: it exists so the scissor and
/// viewport API has one type instead of four loose ints, and both are handed to SDL_GPU
/// field by field.
typedef struct BK_Rect {
  i32 x, y, width, height;
} BK_Rect;

/// RGBA color, one float per channel, nominally [0, 1] -- values above 1 are legal and
/// survive to the GPU, since the draw layer's command format carries color as halves.
typedef struct BK_Color {
  f32 r, g, b, a;
} BK_Color;

/// Constructs a color from four channels.
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

/// Multiplies rgb by a, leaving a alone. The draw layer's command format stores
/// premultiplied color, so this runs once per recorded command.
[[nodiscard]] static inline BK_Color bk_color_premultiply(BK_Color color) {
  return (BK_Color){color.r * color.a, color.g * color.a, color.b * color.a, color.a};
}

/// Component-wise linear interpolation, alpha included. t is not clamped.
[[nodiscard]] static inline BK_Color bk_color_lerp(BK_Color lhs, BK_Color rhs, f32 t) {
  return (BK_Color){bk_lerpf(lhs.r, rhs.r, t), bk_lerpf(lhs.g, rhs.g, t), bk_lerpf(lhs.b, rhs.b, t),
                    bk_lerpf(lhs.a, rhs.a, t)};
}

/// Opaque white.
[[nodiscard]] static inline BK_Color bk_color_white(void) {
  return (BK_Color){1.0f, 1.0f, 1.0f, 1.0f};
}

/// Opaque black.
[[nodiscard]] static inline BK_Color bk_color_black(void) {
  return (BK_Color){0.0f, 0.0f, 0.0f, 1.0f};
}

/// Transparent black -- the accumulation identity, not "no color".
[[nodiscard]] static inline BK_Color bk_color_clear(void) {
  return (BK_Color){0.0f, 0.0f, 0.0f, 0.0f};
}

static_assert(sizeof(BK_Color) == 16, "BK_Color must be four tightly packed f32");
