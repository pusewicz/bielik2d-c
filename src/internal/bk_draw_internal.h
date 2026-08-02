#pragma once
#include <bielik/bk_draw.h>
#include <bielik/bk_math.h>
#include <bielik/bk_types.h>

/// Shape type ids, shared with draw.vert / draw.frag's dispatch. The numbering is
/// bk_draw's own, not Cute Framework's.
typedef enum BK_DrawType {
  BK_DRAW_TYPE_TEXTURE,
  BK_DRAW_TYPE_BOX,
  BK_DRAW_TYPE_CIRCLE,
  BK_DRAW_TYPE_SEGMENT,
  BK_DRAW_TYPE_TRI,
  BK_DRAW_TYPE_ARROW,
} BK_DrawType;

/// One recorded drawable, linked into the frame's arena-allocated chain. Every field is
/// a snapshot taken when the bk_draw_* call ran -- nothing here points at shared state,
/// so later stack changes cannot retroactively alter an earlier record.
typedef struct BK_DrawGeom {
  struct BK_DrawGeom *next;
  i32 record_id; // increments per record; the tiebreaker in the (layer, record_id) sort
  BK_DrawType type;
  BK_Color color; // premultiplied
  // Type-dependent, four slots because TEXTURE needs the most:
  //   TEXTURE  [0]=dst.min [1]=dst.max [2]=src_px.min [3]=src_px.max
  //   BOX      [0]=centre  [1]=half_extents
  //   CIRCLE   [0]=centre
  //   SEGMENT  [0]=a       [1]=b
  //   TRI      [0]=p0      [1]=p1  [2]=p2
  //   ARROW    [0]=a       [1]=b   [2]=(shaft_radius, head_width)
  BK_V2 shape[4];
  f32 radius;      // corner/circle radius
  f32 half_stroke; // 0 => filled
  f32 aa_px;       // antialias band, in pixels
  bool fill;
  i32 layer;
  BK_Rect scissor;
  BK_GfxTexture *texture; // TEXTURE only; nullptr otherwise
  BK_M3x2 transform;      // the camera transform at record time (projection NOT applied)
} BK_DrawGeom;

/// Clears the record chain and resets every state stack to its default. Called at the
/// end of bk__draw_collate, and directly by tests to isolate cases.
void bk__draw_reset(void);

/// Number of records in the current frame's chain. Framework-internal; tests only.
i32 bk__draw_get_geom_count(void);

/// The record at index in record order, or nullptr if out of range. Framework-internal;
/// tests only -- walks the chain, so it is O(index).
const BK_DrawGeom *bk__draw_get_geom(i32 index);
