#pragma once
#include <bielik/bk_math.h> // BK_Aabb, BK_Color, BK_M3x2, BK_Rect, BK_V2
#include <bielik/bk_types.h>

typedef struct BK_GfxTexture BK_GfxTexture;

/// Depth of every bk_draw state stack, including the camera's.
constexpr i32 BK_DRAW_STACK_MAX = 64;

// ---------------------------------------------------------------------------
// Paint order, and one unsupported combination
//
// bk_draw records into a per-frame list the framework submits once, after the render
// callback returns. So everything bk_draw draws paints ON TOP OF everything the same
// frame drew through the raw bk_gfx_* API, no matter which order the calls were made
// in: raw draws cannot be layered over bk_draw output within a frame. Within bk_draw
// itself, bk_draw_push_layer is how to order shapes.
//
// A canvas bound with bk_gfx_bind_canvas is NOT supported alongside bk_draw yet. The
// draw pipelines bake the swapchain's colour format, which a canvas texture (always
// R8G8B8A8_UNORM) may not share -- the swapchain's format is backend-dependent, so a
// backend that happens to pick R8G8B8A8_UNORM would match and drawing would proceed.
// bk__draw_collate detects the mismatch and declines the frame's draws, logging once
// via SDL_Log, rather than drawing into the wrong format -- which was silent on Metal
// and a validation error on Vulkan/D3D12 before this check existed. The real fix (a
// (colour, depth)-keyed pipeline cache) is tracked as pusewicz/bielik2d-c issue #27.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

/// Overrides this frame's projection. The default, applied every frame unless this is
/// called, is bk_m3x2_ortho over the render target's size -- origin at the center, y up,
/// one unit per pixel, matching bk_m3x2_ortho's documented contract. The override is
/// consumed by the frame's collate, so call it every frame you want it; there is no
/// resize event to handle, because the default is recomputed from the current target
/// each frame.
void bk_draw_set_projection(BK_M3x2 projection);

/// Saves the current camera transform. Depth is capped (BK_DRAW_STACK_MAX, 64);
/// overflowing BK_ASSERTs in Debug and is ignored in Release.
void bk_draw_push(void);

/// Restores the camera transform saved by the matching bk_draw_push. Popping the base
/// entry BK_ASSERTs in Debug and leaves the base in place in Release.
void bk_draw_pop(void);

/// The current camera transform -- world space to eye space, projection not applied.
[[nodiscard]] BK_M3x2 bk_draw_peek(void);

/// Translates the camera transform. Composes onto the current top, like the rest of the
/// transform calls: the last call applied is the innermost.
void bk_draw_translate(BK_V2 offset);

/// Rotates the camera transform by radians, counter-clockwise.
void bk_draw_rotate(f32 radians);

/// Scales the camera transform. A zero component makes the transform singular, which is
/// legal: a shape scaled to zero covers no pixels, so subsequent draws are dropped as
/// they are recorded instead of reaching the GPU. Not an error, and nothing is logged.
void bk_draw_scale(BK_V2 scale);

/// Composes transform onto the current camera transform.
void bk_draw_transform(BK_M3x2 transform);

// ---------------------------------------------------------------------------
// State stacks
//
// Each is a plain push/pop/peek triplet over a 64-deep stack reset to its default at
// the end of every frame's collate, so a push/pop imbalance cannot leak into the next
// frame. pop returns the value it removed.
// ---------------------------------------------------------------------------

/// Sets the color for subsequent shapes, and the tint for subsequent textures. Straight
/// (non-premultiplied) alpha -- bk_color_premultiply runs at record time, because the
/// one blend mode is premultiplied. Default: bk_color_white().
void bk_draw_push_color(BK_Color color);
BK_Color bk_draw_pop_color(void);
[[nodiscard]] BK_Color bk_draw_peek_color(void);

/// Sets the layer for subsequent draws. Higher layers paint later. Within one layer,
/// record order decides. Default: 0.
void bk_draw_push_layer(i32 layer);
i32 bk_draw_pop_layer(void);
[[nodiscard]] i32 bk_draw_peek_layer(void);

/// Sets the antialiasing band width in pixels for subsequent shapes. 0 disables it --
/// the pixel-art path. Ignored by bk_draw_texture, whose edges are the sampler's
/// business. Default: 1.0f.
void bk_draw_push_antialias(f32 aa_px);
f32 bk_draw_pop_antialias(void);
[[nodiscard]] f32 bk_draw_peek_antialias(void);

/// Clips subsequent draws to rect, in pixels of the render target with a top-left
/// origin -- the same meaning bk_gfx_set_scissor documents, and the same "width or
/// height <= 0 means no scissor" convention. Default: a zero rect.
void bk_draw_push_scissor(BK_Rect rect);
BK_Rect bk_draw_pop_scissor(void);
[[nodiscard]] BK_Rect bk_draw_peek_scissor(void);

// ---------------------------------------------------------------------------
// Shapes
//
// All coordinates are world space, transformed by the camera transform captured at
// record time. thickness strokes centered on the shape's boundary. radius rounds
// corners; 0 keeps them sharp. Neither is clamped anywhere: a thickness wider than the
// shape spills as far outward as it fills inward, and a radius past a box's smaller
// half-extent drives the SDF's inset extents negative, distorting the silhouette rather
// than saturating it at a capsule. Keep radius below the smaller half-extent.
// ---------------------------------------------------------------------------

void bk_draw_box_fill(BK_Aabb bb, f32 radius);
void bk_draw_box(BK_Aabb bb, f32 thickness, f32 radius);

void bk_draw_circle_fill(BK_V2 center, f32 radius);
void bk_draw_circle(BK_V2 center, f32 radius, f32 thickness);

/// A stroked segment with round caps -- a capsule SDF.
void bk_draw_line(BK_V2 p0, BK_V2 p1, f32 thickness);

void bk_draw_tri_fill(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 radius);
void bk_draw_tri(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 thickness, f32 radius);

/// A directed arrow from a to b: a shaft of the given thickness unioned with a head of
/// the given width, evaluated as one SDF so the seam never double-blends.
void bk_draw_arrow(BK_V2 a, BK_V2 b, f32 thickness, f32 head_width);

/// Draws texture's src_px sub-rect (in texels, top-left origin) onto the dst box in
/// world space, tinted by the current color and transformed by the camera. Passing a
/// src_px covering the whole texture draws it whole; passing sub-rects is how 9-slice
/// composes, and how P3.4's atlas will feed this same path. Each texture change starts a
/// new batch, so this costs one draw per distinct texture until the atlas lands.
///
/// texture's pixels must be **premultiplied**: RGB already scaled by A. The one blend
/// mode is premultiplied and the shader samples the texel as-is, so straight-alpha data
/// (an ordinary decoded PNG) composites over-bright -- a 50%-alpha white texel reads back
/// fully white instead of half-blended, and every soft edge and fade is wrong the same
/// way. Premultiply at load time.
void bk_draw_texture(BK_GfxTexture *texture, BK_Aabb src_px, BK_Aabb dst);
