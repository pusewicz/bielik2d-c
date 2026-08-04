#version 450

layout (location = 0) in float in_corner;

struct Cmd {
  uvec4 meta;  // type, color_rg, payload offset, matrix palette offset
  vec4 shape;  // radius, half-stroke, antialias (world units), unused
  vec4 misc;   // fill, color_ba as float bits, unused, unused
};

// SDL_GPU's SPIR-V convention puts vertex-stage storage buffers in set 0 and uniform
// buffers in set 1; its MSL convention wants [[buffer]] indices as uniforms-first,
// storage-second. spirv-cross assigns MSL indices by (set, binding) sort order, which
// produces exactly the opposite pairing -- a mismatch that fails silently at draw
// time. The MSL build therefore compiles this shader with -DBK_MSL_BINDINGS and
// translates with --msl-decoration-binding, matching shaders/instanced.vert's
// precedent. See DEVIATIONS.md and cmake/shaders.cmake.
#ifdef BK_MSL_BINDINGS
layout (std430, set = 0, binding = 1) readonly buffer cmd_buffer { Cmd cmds[]; };
layout (std430, set = 0, binding = 2) readonly buffer payload_buffer { vec4 payload[]; };
#else
layout (std430, set = 0, binding = 0) readonly buffer cmd_buffer { Cmd cmds[]; };
layout (std430, set = 0, binding = 1) readonly buffer payload_buffer { vec4 payload[]; };
#endif

// The batch's first command index. cmds/payload hold every batch's data back to back
// in one buffer pair (bk__draw_collate), so a batch after the first must offset its
// read by this rather than starting at cmds[0] again. Not SDL_DrawGPUPrimitives'
// first_instance: SDL_gpu.h documents first_instance as incompatible with shader
// built-in instance IDs and says to always pass 0, since SDL forwards the value to
// each backend unnormalized -- see DEVIATIONS.md.
layout (set = 1, binding = 0) uniform batch_uniform { uvec4 u_batch_base; };

layout (location = 0) out vec4 v_pos_uv;   // world pos.xy, uv.zw
layout (location = 1) out vec4 v_ab;       // payload P0
layout (location = 2) out vec4 v_cd;       // payload P1
layout (location = 3) out vec4 v_col;      // premultiplied colour
layout (location = 4) out vec4 v_shape;    // radius, half-stroke, aa, type
layout (location = 5) out float v_fill;

vec4 unpack_half4(uint rg, uint ba) {
  return vec4(unpackHalf2x16(rg), unpackHalf2x16(ba));
}

void main() {
  Cmd cmd = cmds[gl_InstanceIndex + u_batch_base.x];
  uint type = cmd.meta.x;
  uint po = cmd.meta.z;
  int corner = int(in_corner + 0.5);

  vec4 P0 = payload[po];
  vec4 P1 = payload[po + 1u];

  // Corner 0 = (0,0), 1 = (1,0), 2 = (1,1), 3 = (0,1).
  float cx = (corner == 1 || corner == 2) ? 1.0 : 0.0;
  float cy = (corner == 2 || corner == 3) ? 1.0 : 0.0;

  // Conservative coverage inflation: the SDF band can extend this far past the
  // nominal shape, so the quad must too or the antialias edge gets clipped.
  float pad = cmd.shape.x + cmd.shape.y * 2.0 + cmd.shape.z;

  vec2 pos = vec2(0.0);
  vec2 uv = vec2(0.0);

  if (type == 0u) {
    // TEXTURE: exact destination box, no padding -- edges come from the sampler.
    pos = vec2(mix(P0.x, P0.z, cx), mix(P0.y, P0.w, cy));
    uv = vec2(mix(P1.x, P1.z, cx), mix(P1.y, P1.w, cy));
  } else if (type == 1u) {
    // BOX: centre plus padded half-extents. No rotation basis -- rotation lives in
    // the per-command matrix, so the SDF always sees an axis-aligned box.
    vec2 centre = P0.xy;
    vec2 he = P0.zw + vec2(pad);
    pos = centre + he * vec2(cx * 2.0 - 1.0, cy * 2.0 - 1.0);
  } else if (type == 2u) {
    // CIRCLE: padded square around the centre.
    vec2 he = vec2(cmd.shape.x + pad);
    pos = P0.xy + he * vec2(cx * 2.0 - 1.0, cy * 2.0 - 1.0);
  } else if (type == 3u) {
    // SEGMENT: padded AABB of the two endpoints.
    vec2 mn = min(P0.xy, P0.zw) - vec2(pad);
    vec2 mx = max(P0.xy, P0.zw) + vec2(pad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  } else if (type == 4u) {
    // TRI: padded AABB of the three vertices.
    vec2 mn = min(P0.xy, min(P0.zw, P1.xy)) - vec2(pad);
    vec2 mx = max(P0.xy, max(P0.zw, P1.xy)) + vec2(pad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  } else {
    // ARROW: padded AABB of the endpoints; the head fits within max(radius, width).
    float apad = max(P1.x, P1.y) + pad;
    vec2 mn = min(P0.xy, P0.zw) - vec2(apad);
    vec2 mx = max(P0.xy, P0.zw) + vec2(apad);
    pos = vec2(mix(mn.x, mx.x, cx), mix(mn.y, mx.y, cy));
  }

  // Forward MVP from the matrix palette: two vec4s, basis then origin.
  vec4 f0 = payload[cmd.meta.w];
  vec4 f1 = payload[cmd.meta.w + 1u];
  vec2 posH = f0.xy * pos.x + f0.zw * pos.y + f1.xy;

  v_pos_uv = vec4(pos, uv);
  v_ab = P0;
  v_cd = P1;
  v_col = unpack_half4(cmd.meta.y, floatBitsToUint(cmd.misc.y));
  v_shape = vec4(cmd.shape.x, cmd.shape.y, cmd.shape.z, float(type));
  v_fill = cmd.misc.x;
  gl_Position = vec4(posH, 0.0, 1.0);
}
