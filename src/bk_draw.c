#include "bk_draw_shaders.h"
#include "internal/bk_draw_internal.h"
#include "internal/bk_gfx_internal.h"
#include "internal/bk_gfx_texture_internal.h"

#include <bielik/bk_app.h>
#include <bielik/bk_draw.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_math.h>
#include <bielik/bk_types.h>

#include <SDL3/SDL.h>

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
  // No null contract is documented (bk_draw.h), and CLAUDE.md forbids silent failure:
  // without this, a nullptr texture would reach s_write_shape_payload's zero-size
  // guard, which zeroes the UV rect but still records the shape -- rendering as a
  // solid v_col-tinted rect (sampling uv (0,0) of the white fallback) rather than
  // nothing, with no assert and no log.
  BK_ASSERT(texture != nullptr);
  if (texture == nullptr) {
    return;
  }

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

// ---------------------------------------------------------------------------
// Packing: sort by layer, then lay the chain out into GPU-ready arrays
// ---------------------------------------------------------------------------

/// Vec4 slots a record of type needs in BK_DrawPacked.payload, per the layout table on
/// BK_DrawGeom.shape's doc comment. Shared by both packing passes so they can never
/// disagree about sizing.
static i32 s_payload_size(BK_DrawType type) {
  switch (type) {
  case BK_DRAW_TYPE_BOX:
  case BK_DRAW_TYPE_CIRCLE:
  case BK_DRAW_TYPE_SEGMENT:
    return 1;
  case BK_DRAW_TYPE_TEXTURE:
  case BK_DRAW_TYPE_TRI:
  case BK_DRAW_TYPE_ARROW:
    return 2;
  }
  BK_ASSERT(false);
  return 0;
}

/// Writes geom's shape payload at payload[offset .. offset + s_payload_size(geom->type)),
/// packing BK_DrawGeom.shape into vec4s per type: BOX/SEGMENT pack their two BK_V2 slots
/// into one vec4; CIRCLE packs its center with two unused floats; TRI/ARROW/TEXTURE pack
/// their first two BK_V2 slots into one vec4 and the remaining slot(s) into a second.
/// radius and half_stroke travel in BK_DrawCmd.shape instead, not here.
static void s_write_shape_payload(BK_DrawV4 *payload, i32 offset, const BK_DrawGeom *geom) {
  switch (geom->type) {
  case BK_DRAW_TYPE_BOX:
  case BK_DRAW_TYPE_SEGMENT:
    payload[offset] =
        (BK_DrawV4){geom->shape[0].x, geom->shape[0].y, geom->shape[1].x, geom->shape[1].y};
    return;
  case BK_DRAW_TYPE_CIRCLE:
    payload[offset] = (BK_DrawV4){geom->shape[0].x, geom->shape[0].y, 0.0f, 0.0f};
    return;
  case BK_DRAW_TYPE_TRI:
  case BK_DRAW_TYPE_ARROW:
    payload[offset] =
        (BK_DrawV4){geom->shape[0].x, geom->shape[0].y, geom->shape[1].x, geom->shape[1].y};
    payload[offset + 1] = (BK_DrawV4){geom->shape[2].x, geom->shape[2].y, 0.0f, 0.0f};
    return;
  case BK_DRAW_TYPE_TEXTURE: {
    payload[offset] =
        (BK_DrawV4){geom->shape[0].x, geom->shape[0].y, geom->shape[1].x, geom->shape[1].y};

    // src_px arrives in texels (bk_draw_texture's contract), but draw.frag samples a
    // normalised sampler2D -- it must reach the shader as a [0,1] UV rect, not raw
    // texel coordinates.
    i32 tex_w = 0, tex_h = 0;
    bk__gfx_texture_size(geom->texture, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
      // A texture that failed to create, or (in tests) a stand-in pointer with no real
      // dimensions: normalising would divide by zero and produce inf. Nothing valid to
      // sample from either way, so leave the UV rect zeroed instead.
      payload[offset + 1] = (BK_DrawV4){0.0f, 0.0f, 0.0f, 0.0f};
      return;
    }
    f32 w = (f32)tex_w;
    f32 h = (f32)tex_h;

    // dst is a world-space AABB with y up; src_px is texel space with y down. corner
    // cy = 0 maps to dst.min.y (draw.vert's mix), i.e. the world-space BOTTOM, which
    // must sample src_px's bottom -- the LARGER texel y. So unlike x, the y components
    // are swapped here: (min.x, max.y) paired with (max.x, min.y), each normalised into
    // [0,1] by the texture's own size. draw.vert's mix() needs no change for this.
    payload[offset + 1] = (BK_DrawV4){geom->shape[2].x / w, geom->shape[3].y / h,
                                      geom->shape[3].x / w, geom->shape[2].y / h};
    return;
  }
  }
  BK_ASSERT(false);
}

/// Whether two camera transforms are bit-identical -- the matrix palette's dedup test.
static bool s_transform_eq(BK_M3x2 lhs, BK_M3x2 rhs) {
  return lhs.x.x == rhs.x.x && lhs.x.y == rhs.x.y && lhs.y.x == rhs.y.x && lhs.y.y == rhs.y.y &&
         lhs.origin.x == rhs.origin.x && lhs.origin.y == rhs.origin.y;
}

/// Whether two scissor rects are identical -- one of the two things that split a batch.
static bool s_scissor_eq(BK_Rect lhs, BK_Rect rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

/// Packs a single f32 into an IEEE-754 binary16 bit pattern, in the low 16 bits of the
/// result -- an approximation of GLSL's packHalf2x16 per-component, not a bit-for-bit
/// guarantee: this truncates the mantissa rather than rounding to nearest-even, so a
/// result lands exactly on or exactly 1 ulp below the correctly-rounded value, never
/// further and never in the wrong direction. Fine for color, which is what this feeds.
/// Denormals flush to zero rather than being represented -- colors above 1.0
/// (premultiplied, HDR-ish) matter far more than the denormal tail near zero, and there
/// is no portable _Float16 to lean on here.
static u32 s_half_bits(f32 value) {
  union {
    f32 value;
    u32 bits;
  } convert = {.value = value};

  u32 bits = convert.bits;

  u32 sign = (bits >> 16) & 0x8000u;
  i32 exponent = (i32)((bits >> 23) & 0xFFu) - 127 + 15;
  u32 mantissa = bits & 0x7FFFFFu;

  if (exponent <= 0) {
    return sign; // underflow, or a source denormal -- flush to zero
  }
  if (exponent >= 0x1F) {
    return sign | 0x7C00u; // overflow, or a source inf/NaN -- saturate to infinity
  }
  return sign | ((u32)exponent << 10) | (mantissa >> 13);
}

/// Packs two floats into one u32 as two half-precision values -- lo in the low 16 bits,
/// hi in the high 16 -- approximating GLSL's packHalf2x16(vec2(lo, hi)) to within 1 ulp
/// per component (see s_half_bits).
static u32 s_half4_pack(f32 lo, f32 hi) {
  return s_half_bits(lo) | (s_half_bits(hi) << 16);
}

/// Reinterprets bits as an f32, bit-for-bit rather than numerically -- union type punning
/// is well-defined in C, unlike C++. Used to smuggle a packed half4 color word through
/// BK_DrawCmd.misc's f32 slot.
static f32 s_f32_from_bits(u32 bits) {
  union {
    u32 bits;
    f32 value;
  } convert = {.bits = bits};

  return convert.value;
}

bool bk__draw_pack(BK_DrawPacked *out, i32 target_w, i32 target_h) {
  *out = (BK_DrawPacked){0};

  // Snapshot and clear FIRST, before anything can fail or return early. bk_app.c runs
  // bk__gfx_flush() then bk__arena_reset() unconditionally, and flush has early returns
  // (failed command-buffer acquire, null swapchain texture on a minimised window). A
  // chain left linked across a reset points at arena memory the next frame's records
  // will be handed and will overwrite.
  BK_DrawGeom *head = s_draw.head;
  s_draw.head = nullptr;
  s_draw.tail = nullptr;
  if (head == nullptr) {
    return false;
  }

  // bk_m3x2_ortho is undefined for a zero width or height, which a minimized window can
  // produce. Dropping the frame here mirrors bk__gfx_flush's own early return on a null
  // swapchain texture in that same case -- nothing to render into, not an error.
  if (target_w <= 0 || target_h <= 0) {
    return false;
  }

  // Pass one: count. Both the command count and the shape-payload total (in vec4s) are
  // exact; the matrix palette is not counted here (dedup only compares against the
  // immediately preceding record, so an exact total isn't knowable in one pass) and gets
  // an upper bound of 2 vec4s per command instead, which costs nothing to over-reserve
  // within one arena allocation.
  i32 cmd_count = 0;
  i32 shape_payload_total = 0;
  for (BK_DrawGeom *geom = head; geom != nullptr; geom = geom->next) {
    cmd_count++;
    shape_payload_total += s_payload_size(geom->type);
  }

  BK_DrawGeom **sorted = bk_frame_alloc((usize)cmd_count * sizeof *sorted, alignof(BK_DrawGeom *));
  i32 payload_capacity = shape_payload_total + 2 * cmd_count;
  out->cmds = bk_frame_alloc((usize)cmd_count * sizeof *out->cmds, alignof(BK_DrawCmd));
  out->payload = bk_frame_alloc((usize)payload_capacity * sizeof *out->payload, alignof(BK_DrawV4));
  out->batches = bk_frame_alloc((usize)cmd_count * sizeof *out->batches, alignof(BK_DrawBatch));
  if (sorted == nullptr || out->cmds == nullptr || out->payload == nullptr ||
      out->batches == nullptr) {
    // bk_frame_alloc already BK_ASSERTs on failure, but BK_ASSERT wraps SDL_assert,
    // which compiles to nothing in Release -- log unconditionally so a frame that
    // silently renders nothing still leaves a trace.
    SDL_Log("BK: bk__draw_pack: frame arena allocation failed, dropping %d commands", cmd_count);
    *out = (BK_DrawPacked){0};
    return false;
  }

  i32 idx = 0;
  for (BK_DrawGeom *geom = head; geom != nullptr; geom = geom->next) {
    sorted[idx++] = geom;
  }

  // Stable sort by layer -- insertion sort, not qsort, which the C standard does not
  // require to be stable. Only strictly-greater elements shift, so equal layers keep
  // their record order.
  for (i32 i = 1; i < cmd_count; i++) {
    BK_DrawGeom *key = sorted[i];
    i32 scan = i - 1;
    while (scan >= 0 && sorted[scan]->layer > key->layer) {
      sorted[scan + 1] = sorted[scan];
      scan--;
    }
    sorted[scan + 1] = key;
  }

  BK_M3x2 projection =
      s_draw.has_projection ? s_draw.projection : bk_m3x2_ortho((f32)target_w, (f32)target_h);

  // Pass two: fill. Walks the sorted order once, emitting a palette entry whenever the
  // transform changes, a shape payload for every record, and a new batch whenever the
  // texture or scissor changes.
  i32 payload_cursor = 0;
  i32 palette_cursor = shape_payload_total;
  i32 palette_offset = 0;
  f32 palette_scale = 0.0f;
  BK_M3x2 prev_transform = {0};

  i32 batch_count = 0;
  BK_GfxTexture *prev_texture = nullptr;
  BK_Rect prev_scissor = {0};

  for (i32 i = 0; i < cmd_count; i++) {
    const BK_DrawGeom *geom = sorted[i];

    if (i == 0 || !s_transform_eq(geom->transform, prev_transform)) {
      BK_M3x2 mvp = bk_m3x2_mul(projection, geom->transform);
      palette_offset = palette_cursor;
      out->payload[palette_cursor] = (BK_DrawV4){mvp.x.x, mvp.x.y, mvp.y.x, mvp.y.y};
      out->payload[palette_cursor + 1] = (BK_DrawV4){mvp.origin.x, mvp.origin.y, 0.0f, 0.0f};
      palette_cursor += 2;

      // mvp maps world units to NDC ([-1,1]), so a world unit spans |mvp.x| * target_w/2
      // pixels horizontally. Take the larger axis so the band never under-covers.
      f32 scale_x = bk_v2_len(mvp.x) * (f32)target_w * 0.5f;
      f32 scale_y = bk_v2_len(mvp.y) * (f32)target_h * 0.5f;
      palette_scale = bk_maxf(scale_x, scale_y);

      prev_transform = geom->transform;
    }
    f32 aa_world = palette_scale > 0.0f ? geom->aa_px / palette_scale : 0.0f;

    u32 payload_offset = (u32)payload_cursor;
    s_write_shape_payload(out->payload, payload_cursor, geom);
    payload_cursor += s_payload_size(geom->type);

    out->cmds[i] = (BK_DrawCmd){
        .meta = {(u32)geom->type, s_half4_pack(geom->color.r, geom->color.g), payload_offset,
                 (u32)palette_offset},
        .shape = {geom->radius, geom->half_stroke, aa_world, 0.0f},
        .misc = {geom->fill ? 1.0f : 0.0f,
                 s_f32_from_bits(s_half4_pack(geom->color.b, geom->color.a)), 0.0f, 0.0f},
    };

    // Batches split on texture or scissor change only -- not color, layer, antialias, or
    // shape type.
    bool same_batch =
        i > 0 && geom->texture == prev_texture && s_scissor_eq(geom->scissor, prev_scissor);
    if (same_batch) {
      out->batches[batch_count - 1].count++;
    } else {
      out->batches[batch_count++] = (BK_DrawBatch){
          .first = i, .count = 1, .texture = geom->texture, .scissor = geom->scissor};
    }
    prev_texture = geom->texture;
    prev_scissor = geom->scissor;
  }

  out->cmd_count = cmd_count;
  out->payload_count = palette_cursor;
  out->batch_count = batch_count;
  return true;
}

// ---------------------------------------------------------------------------
// GPU: pipelines, corner buffer, and collate's replay through bk_gfx
// ---------------------------------------------------------------------------

// Two triangles' worth of corner indices, per draw.vert's in_corner: corner 0 = (0,0),
// 1 = (1,0), 2 = (1,1), 3 = (0,1). Six entries, not four -- a four-float buffer would
// read out of bounds on the 6-vertex draw.
static constexpr f32 s_draw_corners[6] = {0.0f, 1.0f, 2.0f, 0.0f, 2.0f, 3.0f};

/// Mirrors draw.vert's batch_uniform block: a single batch-base command index, padded
/// to a uvec4 (16 bytes) so std140's block-size rule is satisfied trivially rather than
/// relying on implicit tail padding. Pushed once per batch, before that batch's draw --
/// see bk__draw_collate. Not SDL_DrawGPUPrimitives' first_instance: SDL_gpu.h documents
/// first_instance as incompatible with shader built-in instance IDs and says to always
/// pass 0, since it forwards the value to each backend unnormalized and HLSL's
/// SV_InstanceID is zero-based where Vulkan's and Metal's equivalents fold the offset
/// in. So the offset travels as a uniform instead of relying on a driver convention SDL
/// itself declines to guarantee. See DEVIATIONS.md.
typedef struct BK_DrawBatchUniform {
  u32 base_index;
  u32 _pad[3];
} BK_DrawBatchUniform;

static_assert(sizeof(BK_DrawBatchUniform) == 16, "std140: one uvec4");

static BK_GfxBuffer *s_corner_buffer = nullptr;

/// A pipeline is created lazily per (colour, depth) render-target format pair the
/// frame actually targets -- a canvas's colour format need not match the swapchain's
/// (issue #27), so the draw pipeline can no longer bake a single cached colour format
/// at init. Bounded at 4: bk_gfx_depth_stencil_format probes and returns exactly one
/// format per device, so depth is one of {INVALID, that format}; colour is one of
/// {swapchain format, a canvas's colour texture format (always R8G8B8A8_UNORM)},
/// which may coincide -- at most 2x2 = 4 distinct combinations are reachable.
constexpr i32 BK_DRAW_MAX_PIPELINES = 4;

static struct {
  SDL_GPUTextureFormat color;
  SDL_GPUTextureFormat depth;
  BK_GfxPipeline *pipeline;
} s_pipelines[BK_DRAW_MAX_PIPELINES];

static i32 s_pipeline_count = 0;

static BK_GfxSampler *s_sampler = nullptr;
// Bound for every shape-only batch: draw.frag declares one sampler unconditionally, so
// a batch with no bk_draw_texture call still needs something bound to that slot -- an
// unbound sampler is the same silent all-zero-command-buffer hazard CLAUDE.md documents
// for a resource-count mismatch.
static BK_GfxTexture *s_white_texture = nullptr;
// Created fresh every collate, but not destroyed until the *next* one (or shutdown):
// bk_gfx_bind_vertex_storage_buffer only stores the pointer into this frame's draw
// chain, which bk__gfx_flush replays after collate returns, so destroying these before
// flush runs would free memory flush is about to dereference.
static BK_GfxBuffer *s_cmds_buffer = nullptr;
static BK_GfxBuffer *s_payload_buffer = nullptr;

/// Builds one of the draw pipeline table's entries, identical but for the render
/// target formats it's baked for. color_format/depth_format are SDL_GPU_TEXTUREFORMAT_
/// INVALID only in depth_format's case (no depth attachment); color_format is always a
/// real format -- there is no colourless render target.
static BK_GfxPipeline *s_create_pipeline(SDL_GPUTextureFormat color_format,
                                         SDL_GPUTextureFormat depth_format) {
  BK_GfxVertexBufferLayout layout = {.slot = 0, .pitch = sizeof(f32)};
  BK_GfxVertexAttribute attribute = {
      .location = 0, .buffer_slot = 0, .format = BK_GFX_VERTEX_FORMAT_FLOAT, .offset = 0};

  BK_GfxPipelineDesc desc = {
      .vertex_shader = {.spirv = {bk__draw_vertex_spv, bk__draw_vertex_spv_size, "main"},
                        .msl = {bk__draw_vertex_msl, bk__draw_vertex_msl_size, "main0"},
                        .num_storage_buffers = 2,
                        .num_uniform_buffers = 1},
      .fragment_shader = {.spirv = {bk__draw_fragment_spv, bk__draw_fragment_spv_size, "main"},
                        .msl = {bk__draw_fragment_msl, bk__draw_fragment_msl_size, "main0"},
                        .num_samplers = 1},
      .vertex_buffers = &layout,
      .num_vertex_buffers = 1,
      .vertex_attributes = &attribute,
      .num_vertex_attributes = 1,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = color_format,
      .blend_mode = BK_GFX_BLEND_PREMULTIPLIED,
      .depth_stencil_format = depth_format,
  };
  return bk_gfx_pipeline_create(bk_gpu(), &desc);
}

/// Looks up (or lazily creates) the pipeline baked for this (color_format,
/// depth_format) pair. A cached nullptr (a prior creation failure) is returned as-is,
/// not retried -- bk_gfx_pipeline_create already logs its own failure unconditionally,
/// so retrying every frame would spam that log. On a miss with the table already full
/// (should not happen: the reachable set is bounded at BK_DRAW_MAX_PIPELINES), logs
/// once and returns nullptr without inserting.
static BK_GfxPipeline *s_get_pipeline(SDL_GPUTextureFormat color_format,
                                      SDL_GPUTextureFormat depth_format) {
  for (i32 i = 0; i < s_pipeline_count; i++) {
    if (s_pipelines[i].color == color_format && s_pipelines[i].depth == depth_format) {
      return s_pipelines[i].pipeline;
    }
  }

  if (s_pipeline_count >= BK_DRAW_MAX_PIPELINES) {
    static bool s_logged_table_full = false;
    if (!s_logged_table_full) {
      SDL_Log("BK: bk__draw_collate: draw pipeline table is full (%d entries) -- this is "
              "unexpected, the reachable (colour, depth) set is bounded at %d",
              s_pipeline_count, BK_DRAW_MAX_PIPELINES);
      s_logged_table_full = true;
    }
    return nullptr;
  }

  BK_GfxPipeline *pipeline = s_create_pipeline(color_format, depth_format);
  s_pipelines[s_pipeline_count] =
      (typeof(s_pipelines[0])){.color = color_format, .depth = depth_format, .pipeline = pipeline};
  s_pipeline_count++;
  return pipeline;
}

void bk__draw_init(void) {
  s_corner_buffer =
      bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof s_draw_corners);
  if (s_corner_buffer != nullptr &&
      !bk_gfx_buffer_upload(s_corner_buffer, s_draw_corners, 0, sizeof s_draw_corners)) {
    // Already logged by bk_gfx_buffer_upload. Drop the buffer rather than keep one
    // holding uninitialised GPU memory: collate's missing-resource branch then declines
    // to draw at all, instead of drawing every shape from garbage corner indices.
    bk_gfx_buffer_destroy(s_corner_buffer);
    s_corner_buffer = nullptr;
  }

  s_sampler = bk_gfx_sampler_create(bk_gpu(), BK_GFX_FILTER_LINEAR, BK_GFX_ADDRESS_CLAMP);

  s_white_texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_SAMPLER, 1, 1);
  if (s_white_texture != nullptr) {
    static constexpr u8 white_pixel[4] = {255, 255, 255, 255};
    if (!bk_gfx_texture_upload(s_white_texture, white_pixel)) {
      // Same reasoning as the corner buffer above: an unuploaded fallback texture tints
      // every shape-only batch by whatever the driver left in that texel.
      bk_gfx_texture_destroy(s_white_texture);
      s_white_texture = nullptr;
    }
  }
}

void bk__draw_collate(void) {
  // Free last frame's storage buffers now, before this frame's pack/upload -- not at
  // the end of this function. See their doc comment above: bk__gfx_flush replays the
  // draw chain built below only after this function returns, so destroying at the end
  // of the frame that created them would free the wrapper before flush dereferences
  // it. SDL_GPU itself defers the underlying release past any command buffer still
  // referencing the old handle (same reasoning as bk_gfx.c's swapchain-depth
  // recreate), so no fence wait is needed here either.
  bk_gfx_buffer_destroy(s_cmds_buffer);
  bk_gfx_buffer_destroy(s_payload_buffer);
  s_cmds_buffer = nullptr;
  s_payload_buffer = nullptr;

  i32 target_w = 0, target_h = 0;
  BK_GfxCanvas *canvas = bk__gfx_get_pending_canvas();
  if (canvas != nullptr) {
    bk__gfx_texture_size(bk_gfx_canvas_texture(canvas), &target_w, &target_h);
  } else {
    // Not the swapchain texture's size: bk_gfx only learns that from
    // SDL_WaitAndAcquireGPUSwapchainTexture inside bk__gfx_flush, which runs after
    // this. bk_window_size is cached and refreshed on resize before any app handler.
    bk_window_size(&target_w, &target_h);
  }

  BK_DrawPacked packed = {0};
  if (bk__draw_pack(&packed, target_w, target_h)) {
    // The design spec's §5.0 names bk__gfx_canvas_depth_format(bk__gfx_get_pending_canvas())
    // as the selector, but that call BK_ASSERTs on a nullptr canvas -- which is exactly
    // the common, no-canvas-bound case (see DEVIATIONS.md). Selecting from what flush
    // will actually attach covers both that case and BK_AppDesc.window.depth_stencil.
    BK_GfxPipeline *pipeline = s_get_pipeline(bk__gfx_pending_target_color_format(),
                                              bk__gfx_pending_target_depth_format());

    if (pipeline == nullptr || s_corner_buffer == nullptr || s_sampler == nullptr ||
        s_white_texture == nullptr) {
      // Unlike the corner buffer/sampler/white texture above (all created eagerly by
      // bk__draw_init, which already logged any SDL_GPU failure there), a pipeline is
      // now created lazily at first use inside s_get_pipeline/s_create_pipeline -- so a
      // nullptr here can also mean this frame's first attempt at a never-before-seen
      // (colour, depth) pair just failed, and s_create_pipeline/bk_gfx_pipeline_create
      // already logged that failure. Logged once, not every frame -- the condition is
      // permanent for the process's lifetime once a given pair has failed once (a
      // failed creation is cached, not retried), so this would otherwise be an
      // unthrottled log at frame rate for as long as the app keeps drawing (same
      // pattern as bk_gfx.c's s_logged_acquire_failure).
      static bool s_logged_missing_resource = false;
      if (!s_logged_missing_resource) {
        SDL_Log("BK: bk__draw_collate: a required GPU resource is unavailable, dropping "
                "%d commands",
                packed.cmd_count);
        s_logged_missing_resource = true;
      }
      bk__draw_reset();
      return;
    }

    u32 cmds_size = (u32)packed.cmd_count * (u32)sizeof(BK_DrawCmd);
    u32 payload_size = (u32)packed.payload_count * (u32)sizeof(BK_DrawV4);
    s_cmds_buffer = bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, cmds_size);
    s_payload_buffer =
        bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, payload_size);
    if (s_cmds_buffer == nullptr || s_payload_buffer == nullptr) {
      SDL_Log("BK: bk__draw_collate: storage buffer allocation failed, dropping %d commands",
              packed.cmd_count);
      bk__draw_reset();
      return;
    }
    // Both uploads must land before any batch draws: an upload that failed leaves its
    // buffer holding uninitialised GPU memory, which draw.vert reads as commands --
    // arbitrary types, offsets and transforms, not nothing.
    if (!bk_gfx_buffer_upload(s_cmds_buffer, packed.cmds, 0, cmds_size) ||
        !bk_gfx_buffer_upload(s_payload_buffer, packed.payload, 0, payload_size)) {
      SDL_Log("BK: bk__draw_collate: storage buffer upload failed, dropping %d commands",
              packed.cmd_count);
      bk__draw_reset();
      return;
    }

    for (i32 i = 0; i < packed.batch_count; i++) {
      const BK_DrawBatch *batch = &packed.batches[i];
      bk_gfx_bind_pipeline(pipeline);
      bk_gfx_bind_vertex_buffer(s_corner_buffer);
      bk_gfx_bind_vertex_storage_buffer(s_cmds_buffer, 0);
      bk_gfx_bind_vertex_storage_buffer(s_payload_buffer, 1);
      // cmds/payload hold every batch's commands back to back in one buffer pair, so
      // draw.vert must offset its cmds[] read by this batch's start -- gl_InstanceIndex
      // alone always counts from 0 within a single draw call, on every batch after the
      // first. See BK_DrawBatchUniform's doc comment for why this travels as a uniform
      // rather than SDL_DrawGPUPrimitives' first_instance parameter.
      BK_DrawBatchUniform batch_uniform = {.base_index = (u32)batch->first};
      bk_gfx_push_vertex_uniform(&batch_uniform, sizeof batch_uniform);
      bk_gfx_bind_texture(batch->texture != nullptr ? batch->texture : s_white_texture, s_sampler);
      bk_gfx_set_scissor(batch->scissor);
      // bk_gfx's bound state is sticky and only cleared inside flush, which runs after
      // this -- an app that called bk_gfx_set_viewport before its render callback
      // returned would otherwise leak into every batch here. bk_draw has no per-batch
      // viewport of its own, so reset to "full target" (a zero rect) explicitly rather
      // than inheriting whatever the app last set.
      bk_gfx_set_viewport((BK_Rect){0});
      bk_gfx_draw_instanced(6, batch->count);
    }
  }
  bk__draw_reset();
}

void bk__draw_shutdown(void) {
  // Not redundant with collate's own reset: bk__iterate returns early when update or
  // post_update returns non-BK_CONTINUE, skipping collate entirely. Without this, an app
  // that records from update and quits on the same tick leaves the chain pointing into
  // memory bk__arena_free is about to release, for a second bk_run in the same process
  // to walk.
  bk__draw_reset();

  bk_gfx_buffer_destroy(s_cmds_buffer);
  bk_gfx_buffer_destroy(s_payload_buffer);
  s_cmds_buffer = nullptr;
  s_payload_buffer = nullptr;

  bk_gfx_texture_destroy(s_white_texture);
  s_white_texture = nullptr;
  bk_gfx_sampler_destroy(s_sampler);
  s_sampler = nullptr;
  for (i32 i = 0; i < s_pipeline_count; i++) {
    bk_gfx_pipeline_destroy(s_pipelines[i].pipeline);
  }
  s_pipeline_count = 0;
  SDL_memset(s_pipelines, 0, sizeof s_pipelines);
  bk_gfx_buffer_destroy(s_corner_buffer);
  s_corner_buffer = nullptr;
}
