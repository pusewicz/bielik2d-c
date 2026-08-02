#version 450

// Per-instance quad data, read out of a storage buffer indexed by gl_InstanceIndex.
// std430 so the array packs tightly; SDL_GPU binds vertex-stage storage buffers at
// set 0, and vertex-stage uniform buffers at set 1.
struct Quad {
    vec2 center;
    vec2 half_size;
    vec4 color;
};

// SDL_GPU's SPIR-V convention puts vertex-stage storage buffers in set 0 and uniform
// buffers in set 1; its MSL convention wants [[buffer]] indices as uniforms-first,
// storage-second. spirv-cross assigns MSL indices by (set, binding) sort order, which
// produces exactly the opposite pairing -- a mismatch that fails silently at draw time.
// The MSL build therefore compiles this shader with -DBK_MSL_BINDINGS and translates
// with --msl-decoration-binding, which uses the binding number directly as the MSL
// index. See DEVIATIONS.md and cmake/shaders.cmake.
#ifdef BK_MSL_BINDINGS
layout(std430, set = 0, binding = 1) readonly buffer quad_buffer {
#else
layout(std430, set = 0, binding = 0) readonly buffer quad_buffer {
#endif
    Quad quads[];
};

layout(set = 1, binding = 0) uniform uniform_block {
    // A BK_M3x2 as two vec4s: (x.x, x.y, y.x, y.y) and (origin.x, origin.y, pad, pad).
    // std140 wants vec4 alignment, so the 6 floats travel as 8.
    vec4 mvp_basis;
    vec4 mvp_origin;
};

layout(location = 0) out vec4 out_color;

void main() {
    Quad quad = quads[gl_InstanceIndex];

    // Unit quad corners from the vertex index, as a triangle strip: 0,1,2,3.
    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
                       (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
    vec2 world = quad.center + corner * quad.half_size;

    // Apply the 2D affine transform: x * p.x + y * p.y + origin.
    vec2 clip = vec2(mvp_basis.x * world.x + mvp_basis.z * world.y + mvp_origin.x,
                     mvp_basis.y * world.x + mvp_basis.w * world.y + mvp_origin.y);

    gl_Position = vec4(clip, 0.0, 1.0);
    out_color = quad.color;
}
