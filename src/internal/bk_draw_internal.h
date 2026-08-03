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
  f32 half_stroke; // stroke half-width; 0 on unstroked records. NOT the fill
                   // discriminator -- see .fill. A line is a filled capsule:
                   // fill = true with half_stroke != 0.
  f32 aa_px;       // antialias band, in pixels
  bool fill;       // the fill/stroke discriminator the packer and shader branch on
  i32 layer;
  BK_Rect scissor;
  BK_GfxTexture *texture; // TEXTURE only; nullptr otherwise
  BK_M3x2 transform;      // the camera transform at record time (projection NOT applied)
} BK_DrawGeom;

/// Clears the record chain, resets every state stack to its default, and drops any
/// projection override set this frame -- bk_draw.h documents that override as consumed
/// by the frame's collate. Called at the end of bk__draw_collate, and directly by tests
/// to isolate cases.
void bk__draw_reset(void);

/// Number of records in the current frame's chain. Framework-internal; tests only.
i32 bk__draw_get_geom_count(void);

/// The record at index in record order, or nullptr if out of range. Framework-internal;
/// tests only -- walks the chain, so it is O(index).
const BK_DrawGeom *bk__draw_get_geom(i32 index);

/// One GPU command, mirroring `Cmd` in draw.vert (std430, three vec4s). Colors travel
/// as packed half4 -- two packHalf2x16 words, rg in meta[1] and ba in misc[1] -- rather
/// than unorm8, so premultiplied channels above 1.0 survive the trip.
typedef struct BK_DrawCmd {
  u32 meta[4];  // type, color_rg, payload offset, matrix palette offset -- the latter two
                // are both absolute vec4 indices into BK_DrawPacked.payload, the same
                // array, not two separate index spaces.
  f32 shape[4]; // radius, half-stroke, antialias band (world units), unused
  f32 misc[4];  // fill (0 or 1), color_ba reinterpreted as f32, 2 unused
} BK_DrawCmd;

static_assert(sizeof(BK_DrawCmd) == 48, "std430 layout: three vec4s");

/// A payload slot. Named rather than using f32[4] so the packer's intent reads clearly.
typedef struct BK_DrawV4 {
  f32 x, y, z, w;
} BK_DrawV4;

/// A contiguous run of commands sharing a texture and a scissor -- one instanced draw.
typedef struct BK_DrawBatch {
  i32 first;
  i32 count;
  BK_GfxTexture *texture; // nullptr for shape-only batches
  BK_Rect scissor;
} BK_DrawBatch;

/// The packer's output. Every array is a single frame-arena allocation, valid until the
/// next bk__arena_reset. payload holds every command's shape vec4s first, back to back
/// in cmds order, followed by one matrix-palette pair per distinct camera transform: a
/// (mvp.x.x, mvp.x.y, mvp.y.x, mvp.y.y) "basis" vec4, then a (mvp.origin.x, mvp.origin.y,
/// 0, 0) "origin" vec4 -- the same convention shaders/instanced.vert's mvp_basis/
/// mvp_origin uniforms already use. draw.vert indexes both regions through the same
/// buffer, via BK_DrawCmd.meta's two offsets.
typedef struct BK_DrawPacked {
  BK_DrawCmd *cmds;
  i32 cmd_count;
  BK_DrawV4 *payload;
  i32 payload_count;
  BK_DrawBatch *batches;
  i32 batch_count;
} BK_DrawPacked;

/// Snapshots and clears the record chain, stable-sorts it by (layer, record_id), and
/// packs it into contiguous arrays sized exactly. target_w/target_h are the render
/// target's pixel dimensions, used to convert the antialias band from pixels into the
/// world units the shader evaluates in, and to build the default projection
/// (bk_m3x2_ortho) when no override is set -- both undefined for a non-positive
/// dimension, so callers must pass the actual drawable size, not a stale or zeroed one.
/// Returns false if there was nothing to draw, target_w/target_h was <= 0 (a minimized
/// window, mirroring bk__gfx_flush's own early return in that case), or an arena
/// allocation failed; *out is zeroed in that case. Does NOT touch the GPU, so tests can
/// call it with no device.
bool bk__draw_pack(BK_DrawPacked *out, i32 target_w, i32 target_h);

// ---------------------------------------------------------------------------
// GPU: pipelines, corner buffer, and the collate that replays a frame's packed
// commands through bk_gfx.
// ---------------------------------------------------------------------------

/// Creates the corner buffer, the default texture sampler, and a 1x1 white fallback
/// texture bound for shape-only batches (draw.frag declares one sampler
/// unconditionally, so every batch must bind something). Pipelines are not created
/// here -- they're looked up lazily from a (colour, depth)-keyed table on first use
/// (see bk_gfx.h's bk_gfx_bind_canvas doc comment for the format-match assert that
/// table exists to satisfy). Called once from bk_app.c after the GPU device exists,
/// alongside bk__gfx_configure_swapchain_depth.
void bk__draw_init(void);

/// Packs this frame's record chain and replays it through bk_gfx: one
/// bk_gfx_bind_pipeline/bind_vertex_buffer/bind_vertex_storage_buffer/bind_texture/
/// set_scissor/draw_instanced sequence per batch. Called once per frame from bk_app.c,
/// between the app's render callback and bk__gfx_flush. Always resets the record chain
/// and every state stack before returning (bk__draw_reset), even when there was
/// nothing to draw or a GPU resource wasn't available -- a push/pop imbalance or a
/// stale projection override must never leak into the next frame regardless of this
/// frame's GPU outcome.
void bk__draw_collate(void);

/// Destroys both pipelines, the sampler, the fallback texture, the corner buffer, and
/// whichever of this frame's two storage buffers collate left held (see bk__draw_collate's
/// doc comment on why they outlive the collate call that created them), and nulls them
/// all. Also clears the record chain (bk__draw_reset), because a run that ends from
/// update never reaches collate. Called once from bk_app.c, immediately before
/// bk__gfx_shutdown.
void bk__draw_shutdown(void);
