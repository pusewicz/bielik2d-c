#include "internal/bk_draw_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_draw.h>
#include <bielik/bk_math.h>
#include <bielik/bk_types.h>

// The record layer: state stacks plus this frame's chain of recorded drawables. No GPU,
// no packing -- s_record snapshots the stacks into a BK_DrawGeom so later stack changes
// cannot retroactively alter an earlier record. See the design spec's section 5.

/// Every state stack, plus the current frame's record chain. Stacks store their base
/// (default) value at index 0 and start with count == 1, so peek/pop never underflow an
/// empty array.
typedef struct BK_DrawState {
  BK_DrawGeom *head;
  BK_DrawGeom *tail;
  i32 record_count;

  BK_M3x2 transforms[BK_DRAW_STACK_MAX];
  i32 transform_count;
  BK_Color colors[BK_DRAW_STACK_MAX];
  i32 color_count;
  i32 layers[BK_DRAW_STACK_MAX];
  i32 layer_count;
  f32 antialias[BK_DRAW_STACK_MAX];
  i32 antialias_count;
  BK_Rect scissors[BK_DRAW_STACK_MAX];
  i32 scissor_count;

  BK_M3x2 projection;
  bool has_projection;
} BK_DrawState;

static BK_DrawState s_draw;
static bool s_initialized_stacks;

/// Seeds every stack's base entry the first time anything touches s_draw, so tests (and
/// the first frame) need no explicit init. Also used by bk__draw_reset to reseed after
/// clearing.
static void s_ensure_stacks(void) {
  if (s_initialized_stacks) {
    return;
  }
  s_draw.transforms[0] = bk_m3x2_identity();
  s_draw.transform_count = 1;
  s_draw.colors[0] = bk_color_white();
  s_draw.color_count = 1;
  s_draw.layers[0] = 0;
  s_draw.layer_count = 1;
  s_draw.antialias[0] = 1.0f;
  s_draw.antialias_count = 1;
  s_draw.scissors[0] = (BK_Rect){0};
  s_draw.scissor_count = 1;
  s_initialized_stacks = true;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void bk_draw_set_projection(BK_M3x2 projection) {
  s_ensure_stacks();
  s_draw.projection = projection;
  s_draw.has_projection = true;
}

void bk_draw_push(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.transform_count < BK_DRAW_STACK_MAX);
  if (s_draw.transform_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.transforms[s_draw.transform_count] = s_draw.transforms[s_draw.transform_count - 1];
  s_draw.transform_count++;
}

void bk_draw_pop(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.transform_count > 1);
  if (s_draw.transform_count <= 1) {
    return;
  }
  s_draw.transform_count--;
}

BK_M3x2 bk_draw_peek(void) {
  s_ensure_stacks();
  return s_draw.transforms[s_draw.transform_count - 1];
}

void bk_draw_transform(BK_M3x2 transform) {
  s_ensure_stacks();
  BK_M3x2 *top = &s_draw.transforms[s_draw.transform_count - 1];
  *top = bk_m3x2_mul(*top, transform);
}

void bk_draw_translate(BK_V2 offset) {
  bk_draw_transform(bk_m3x2_translation(offset));
}

void bk_draw_rotate(f32 radians) {
  bk_draw_transform(bk_m3x2_rotation(radians));
}

void bk_draw_scale(BK_V2 scale) {
  bk_draw_transform(bk_m3x2_scale(scale));
}

// ---------------------------------------------------------------------------
// State stacks
// ---------------------------------------------------------------------------

void bk_draw_push_color(BK_Color color) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.color_count < BK_DRAW_STACK_MAX);
  if (s_draw.color_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.colors[s_draw.color_count++] = color;
}

BK_Color bk_draw_pop_color(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.color_count > 1);
  if (s_draw.color_count <= 1) {
    return s_draw.colors[0];
  }
  return s_draw.colors[--s_draw.color_count];
}

BK_Color bk_draw_peek_color(void) {
  s_ensure_stacks();
  return s_draw.colors[s_draw.color_count - 1];
}

void bk_draw_push_layer(i32 layer) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.layer_count < BK_DRAW_STACK_MAX);
  if (s_draw.layer_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.layers[s_draw.layer_count++] = layer;
}

i32 bk_draw_pop_layer(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.layer_count > 1);
  if (s_draw.layer_count <= 1) {
    return s_draw.layers[0];
  }
  return s_draw.layers[--s_draw.layer_count];
}

i32 bk_draw_peek_layer(void) {
  s_ensure_stacks();
  return s_draw.layers[s_draw.layer_count - 1];
}

void bk_draw_push_antialias(f32 aa_px) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.antialias_count < BK_DRAW_STACK_MAX);
  if (s_draw.antialias_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.antialias[s_draw.antialias_count++] = aa_px;
}

f32 bk_draw_pop_antialias(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.antialias_count > 1);
  if (s_draw.antialias_count <= 1) {
    return s_draw.antialias[0];
  }
  return s_draw.antialias[--s_draw.antialias_count];
}

f32 bk_draw_peek_antialias(void) {
  s_ensure_stacks();
  return s_draw.antialias[s_draw.antialias_count - 1];
}

void bk_draw_push_scissor(BK_Rect rect) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.scissor_count < BK_DRAW_STACK_MAX);
  if (s_draw.scissor_count >= BK_DRAW_STACK_MAX) {
    return;
  }
  s_draw.scissors[s_draw.scissor_count++] = rect;
}

BK_Rect bk_draw_pop_scissor(void) {
  s_ensure_stacks();
  BK_ASSERT(s_draw.scissor_count > 1);
  if (s_draw.scissor_count <= 1) {
    return s_draw.scissors[0];
  }
  return s_draw.scissors[--s_draw.scissor_count];
}

BK_Rect bk_draw_peek_scissor(void) {
  s_ensure_stacks();
  return s_draw.scissors[s_draw.scissor_count - 1];
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

/// Snapshots the current stacks into a new BK_DrawGeom, links it into the frame's chain,
/// and returns it for the caller to fill in the shape payload. Returns nullptr if the
/// camera transform is singular (culled -- not an error) or the frame arena is
/// exhausted (already logged and asserted by bk_frame_alloc).
static BK_DrawGeom *s_record(BK_DrawType type) {
  s_ensure_stacks();
  BK_M3x2 transform = s_draw.transforms[s_draw.transform_count - 1];

  // Singular-transform cull: a shape scaled to zero rasterizes to zero area. Dropping
  // it here skips packing and uploading a record that draws nothing. Not an error --
  // no log, no assert.
  f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
  if (det == 0.0f) {
    return nullptr;
  }

  BK_DrawGeom *geom = bk_frame_alloc(sizeof *geom, alignof(BK_DrawGeom));
  if (geom == nullptr) {
    return nullptr;
  }
  *geom = (BK_DrawGeom){
      .record_id = s_draw.record_count++,
      .type = type,
      .color = bk_color_premultiply(s_draw.colors[s_draw.color_count - 1]),
      .aa_px = s_draw.antialias[s_draw.antialias_count - 1],
      .layer = s_draw.layers[s_draw.layer_count - 1],
      .scissor = s_draw.scissors[s_draw.scissor_count - 1],
      .transform = transform,
  };
  if (s_draw.tail == nullptr) {
    s_draw.head = geom;
  } else {
    s_draw.tail->next = geom;
  }
  s_draw.tail = geom;
  return geom;
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

void bk_draw_box_fill(BK_Aabb bb, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_BOX);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = bk_aabb_center(bb);
  geom->shape[1] = bk_aabb_half_extents(bb);
  geom->radius = radius;
  geom->fill = true;
}

void bk_draw_box(BK_Aabb bb, f32 thickness, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_BOX);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = bk_aabb_center(bb);
  geom->shape[1] = bk_aabb_half_extents(bb);
  geom->radius = radius;
  geom->half_stroke = thickness * 0.5f;
  geom->fill = false;
}

void bk_draw_circle_fill(BK_V2 center, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_CIRCLE);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = center;
  geom->radius = radius;
  geom->fill = true;
}

void bk_draw_circle(BK_V2 center, f32 radius, f32 thickness) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_CIRCLE);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = center;
  geom->radius = radius;
  geom->half_stroke = thickness * 0.5f;
  geom->fill = false;
}

void bk_draw_line(BK_V2 p0, BK_V2 p1, f32 thickness) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_SEGMENT);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = p0;
  geom->shape[1] = p1;
  geom->half_stroke = thickness * 0.5f;
  geom->fill = true; // a line is a filled capsule, not a stroked outline
}

void bk_draw_tri_fill(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_TRI);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = p0;
  geom->shape[1] = p1;
  geom->shape[2] = p2;
  geom->radius = radius;
  geom->fill = true;
}

void bk_draw_tri(BK_V2 p0, BK_V2 p1, BK_V2 p2, f32 thickness, f32 radius) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_TRI);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = p0;
  geom->shape[1] = p1;
  geom->shape[2] = p2;
  geom->radius = radius;
  geom->half_stroke = thickness * 0.5f;
  geom->fill = false;
}

void bk_draw_arrow(BK_V2 a, BK_V2 b, f32 thickness, f32 head_width) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_ARROW);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = a;
  geom->shape[1] = b;
  geom->shape[2] = bk_v2(thickness * 0.5f, head_width); // shaft_radius, head_width
  geom->fill = true; // shaft + head is one SDF, not a stroked outline
}

void bk_draw_texture(BK_GfxTexture *texture, BK_Aabb src_px, BK_Aabb dst) {
  BK_DrawGeom *geom = s_record(BK_DRAW_TYPE_TEXTURE);
  if (geom == nullptr) {
    return;
  }
  geom->shape[0] = dst.min;
  geom->shape[1] = dst.max;
  geom->shape[2] = src_px.min;
  geom->shape[3] = src_px.max;
  geom->texture = texture;
  geom->fill = true;
}

// ---------------------------------------------------------------------------
// Framework-internal
// ---------------------------------------------------------------------------

void bk__draw_reset(void) {
  // Records live in the frame arena, which bk__arena_reset recycles -- nothing here is
  // freed, only unlinked.
  s_draw.head = nullptr;
  s_draw.tail = nullptr;
  s_draw.record_count = 0;
  // The projection override is consumed by each collate (bk_draw_set_projection's
  // contract): clearing the flag here means an un-set frame recomputes the default
  // rather than inheriting a stale override from a previous frame.
  s_draw.has_projection = false;
  s_initialized_stacks = false;
  s_ensure_stacks();
}

i32 bk__draw_get_geom_count(void) {
  return s_draw.record_count;
}

const BK_DrawGeom *bk__draw_get_geom(i32 index) {
  if (index < 0) {
    return nullptr;
  }
  const BK_DrawGeom *geom = s_draw.head;
  for (i32 i = 0; i < index && geom != nullptr; i++) {
    geom = geom->next;
  }
  return geom;
}
