#version 450

layout (location = 0) in vec4 v_pos_uv;
layout (location = 1) in vec4 v_ab;
layout (location = 2) in vec4 v_cd;
layout (location = 3) in vec4 v_col;
layout (location = 4) in vec4 v_shape;
layout (location = 5) in float v_fill;

layout (location = 0) out vec4 result;

layout (set = 2, binding = 0) uniform sampler2D u_image;

float safe_div(float a, float b) { return b == 0.0 ? 0.0 : a / b; }
float safe_len(vec2 v) { float d = dot(v, v); return d == 0.0 ? 0.0 : sqrt(d); }
float det2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

float distance_aabb(vec2 p, vec2 he) {
  vec2 d = abs(p) - he;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Referenced from: https://www.shadertoy.com/view/3tdSDj
float distance_segment(vec2 p, vec2 a, vec2 b) {
  vec2 n = b - a;
  vec2 pa = p - a;
  float d = safe_div(dot(pa, n), dot(n, n));
  float h = clamp(d, 0.0, 1.0);
  return safe_len(pa - h * n);
}

// Referenced from: https://www.shadertoy.com/view/XsXSz4
float distance_triangle(vec2 p, vec2 a, vec2 b, vec2 c) {
  vec2 e0 = b - a, e1 = c - b, e2 = a - c;
  vec2 v0 = p - a, v1 = p - b, v2 = p - c;
  vec2 pq0 = v0 - e0 * clamp(safe_div(dot(v0, e0), dot(e0, e0)), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(safe_div(dot(v1, e1), dot(e1, e1)), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(safe_div(dot(v2, e2), dot(e2, e2)), 0.0, 1.0);
  float s = det2(e0, e2);
  vec2 d = min(min(vec2(dot(pq0, pq0), s * det2(v0, e0)),
                   vec2(dot(pq1, pq1), s * det2(v1, e1))),
                   vec2(dot(pq2, pq2), s * det2(v2, e2)));
  return -sqrt(d.x) * sign(d.y);
}

// Shaft capsule unioned with a triangular head, as one SDF so the seam never
// double-blends. r = shaft radius, w = head length and half-width.
float distance_arrow(vec2 p, vec2 a, vec2 b, float r, float w) {
  vec2 d = b - a;
  float l = safe_len(d);
  vec2 n = l == 0.0 ? vec2(0.0) : d / l;
  vec2 base = b - n * w;
  vec2 t = vec2(-n.y, n.x) * w;
  float ds = distance_segment(p, a, base) - r;
  float dt = distance_triangle(p, b, base + t, base - t);
  return min(ds, dt);
}

void main() {
  uint type = uint(v_shape.w + 0.5);
  vec2 pos = v_pos_uv.xy;
  float radius = v_shape.x;
  float half_stroke = v_shape.y;
  float aa = v_shape.z;

  if (type == 0u) {
    result = texture(u_image, v_pos_uv.zw) * v_col;
    return;
  }

  float dist;
  if (type == 1u) {
    dist = distance_aabb(pos - v_ab.xy, v_ab.zw - vec2(radius)) - radius;
  } else if (type == 2u) {
    dist = safe_len(pos - v_ab.xy) - radius;
  } else if (type == 3u) {
    dist = distance_segment(pos, v_ab.xy, v_ab.zw) - half_stroke;
  } else if (type == 4u) {
    dist = distance_triangle(pos, v_ab.xy, v_ab.zw, v_cd.xy) - radius;
  } else {
    dist = distance_arrow(pos, v_ab.xy, v_ab.zw, v_cd.x, v_cd.y);
  }

  // A stroked shape is the shell around its own isosurface.
  if (v_fill < 0.5) {
    dist = abs(dist) - half_stroke;
  }

  // aa arrives in world units (the CPU divided the pixel band by the composed scale),
  // which is the space dist is measured in.
  float coverage = aa > 0.0 ? 1.0 - smoothstep(-aa, aa, dist) : (dist <= 0.0 ? 1.0 : 0.0);
  if (coverage <= 0.0) {
    discard;
  }
  result = v_col * coverage;
}
