#include "bk_draw_shaders.h"
#include "bk_test.h"
#include "internal/bk_app_internal.h" // bk__arena_reset, for the arena-growth regression
#include "internal/bk_draw_internal.h"

#include <bielik/bk_draw.h>
#include <bielik/bk_math.h>

#include <stddef.h> // offsetof, used by Task 3's layout test
#include <stdio.h>

static void test_color_stack_pushes_pops_and_peeks(void) {
  bk__draw_reset();
  REQUIRE(bk_draw_peek_color().r == 1.0f); // default is white

  bk_draw_push_color((BK_Color){0.25f, 0.5f, 0.75f, 1.0f});
  REQUIRE(bk_draw_peek_color().r == 0.25f);

  bk_draw_push_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});
  REQUIRE(bk_draw_peek_color().r == 0.0f);

  BK_Color popped = bk_draw_pop_color();
  REQUIRE(popped.r == 0.0f);                // pop returns what it removed
  REQUIRE(bk_draw_peek_color().r == 0.25f); // and restores the one beneath

  bk_draw_pop_color();
  REQUIRE(bk_draw_peek_color().r == 1.0f);
}

static void test_records_snapshot_their_own_state(void) {
  bk__draw_reset();

  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_push_layer(3);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  bk_draw_pop_layer();
  bk_draw_pop_color();

  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);

  REQUIRE(bk__draw_get_geom_count() == 2);

  // Each record kept the state bound when *it* was recorded. If records referenced
  // shared state, both would report the post-pop defaults.
  const BK_DrawGeom *first = bk__draw_get_geom(0);
  const BK_DrawGeom *second = bk__draw_get_geom(1);
  REQUIRE(first != nullptr && second != nullptr);
  REQUIRE(first->layer == 3);
  REQUIRE(second->layer == 0);
  REQUIRE(first->color.g == 0.0f);  // red, premultiplied
  REQUIRE(second->color.g == 1.0f); // white
  // Order matches call order, not reverse.
  REQUIRE(first->record_id == 0);
  REQUIRE(second->record_id == 1);
}

static void test_transform_composes_and_restores(void) {
  bk__draw_reset();
  REQUIRE(bk_draw_peek().origin.x == 0.0f);

  bk_draw_push();
  bk_draw_translate(bk_v2(10.0f, 0.0f));
  REQUIRE_NEAR(bk_draw_peek().origin.x, 10.0f, 1e-5);

  bk_draw_push();
  bk_draw_translate(bk_v2(5.0f, 0.0f));
  REQUIRE_NEAR(bk_draw_peek().origin.x, 15.0f, 1e-5); // composes, not replaces

  bk_draw_pop();
  REQUIRE_NEAR(bk_draw_peek().origin.x, 10.0f, 1e-5);
  bk_draw_pop();
  REQUIRE_NEAR(bk_draw_peek().origin.x, 0.0f, 1e-5);
}

static void test_records_capture_the_camera_transform(void) {
  bk__draw_reset();

  bk_draw_push();
  bk_draw_translate(bk_v2(100.0f, 0.0f));
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 4.0f);
  bk_draw_pop();

  const BK_DrawGeom *geom = bk__draw_get_geom(0);
  REQUIRE(geom != nullptr);
  REQUIRE_NEAR(geom->transform.origin.x, 100.0f, 1e-5);
}

static void test_singular_transform_is_culled(void) {
  bk__draw_reset();

  bk_draw_push();
  bk_draw_scale(bk_v2(0.0f, 1.0f)); // collapses x -- determinant is zero
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  bk_draw_pop();

  // Dropped at record time: no log, no assert. A sprite scaled to zero is legitimate
  // game state that correctly draws nothing.
  REQUIRE(bk__draw_get_geom_count() == 0);

  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  REQUIRE(bk__draw_get_geom_count() == 1);
}

static void test_reset_clears_records_and_stacks(void) {
  bk__draw_reset();
  bk_draw_push_color((BK_Color){0.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_push_layer(9);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);

  bk__draw_reset();

  REQUIRE(bk__draw_get_geom_count() == 0);
  REQUIRE(bk__draw_get_geom(0) == nullptr);
  // A push/pop imbalance must not leak into the next frame.
  REQUIRE(bk_draw_peek_color().r == 1.0f);
  REQUIRE(bk_draw_peek_layer() == 0);
}

static void test_packed_cmd_matches_the_shader_layout(void) {
  // Three vec4s, std430. draw.vert's `Cmd` struct must match this byte-for-byte.
  REQUIRE(sizeof(BK_DrawCmd) == 48);
  REQUIRE(offsetof(BK_DrawCmd, meta) == 0);
  REQUIRE(offsetof(BK_DrawCmd, shape) == 16);
  REQUIRE(offsetof(BK_DrawCmd, misc) == 32);
}

static void test_layers_sort_above_record_order(void) {
  bk__draw_reset();

  bk_draw_push_layer(5);
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f); // record 0
  bk_draw_pop_layer();
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f); // record 1, layer 0

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 2);
  // Layer 0 paints first even though it was recorded second.
  REQUIRE(packed.cmds[0].meta[0] == BK_DRAW_TYPE_CIRCLE);
  REQUIRE(packed.cmds[1].meta[0] == BK_DRAW_TYPE_BOX);
}

static void test_equal_layers_keep_record_order(void) {
  bk__draw_reset();

  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f);                            // record 0
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f); // record 1
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 2.0f);                            // record 2

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 3);
  // The sort is stable, so equal layers preserve the order they were recorded in.
  REQUIRE(packed.cmds[0].meta[0] == BK_DRAW_TYPE_CIRCLE);
  REQUIRE(packed.cmds[1].meta[0] == BK_DRAW_TYPE_BOX);
  REQUIRE(packed.cmds[2].meta[0] == BK_DRAW_TYPE_CIRCLE);
  // The shape-type sequence alone is palindromic, so an anti-stable sort that reverses
  // equal-layer records maps it to itself and this test would pass under the very
  // regression it names. The two circles' radii (shape[0], not the payload -- a CIRCLE's
  // payload slot holds only its centre) are what tell them apart.
  REQUIRE(packed.cmds[0].shape[0] == 1.0f);
  REQUIRE(packed.cmds[1].shape[0] == 0.0f); // the box's corner radius
  REQUIRE(packed.cmds[2].shape[0] == 2.0f);
}

static void test_batches_split_on_texture_and_scissor_only(void) {
  // Wide enough to stand in for a real BK_GfxTexture: the packer's TEXTURE case now
  // reads texture->width/height (bk__gfx_texture_size) to normalise src_px into UVs, so
  // a bare int-sized dummy would be an out-of-bounds read. Zeroed, so
  // bk__gfx_texture_size reports width=height=0 -- exercises the packer's zero-size
  // guard rather than crashing. alignas(max_align_t): the cast to BK_GfxTexture *
  // relies on this dummy satisfying whatever alignment the real (opaque, incomplete
  // here) struct actually needs -- a plain u8[] only guarantees 1-byte alignment.
  static alignas(max_align_t) u8 dummy_a[256], dummy_b[256];
  BK_GfxTexture *texture_a = (BK_GfxTexture *)dummy_a;
  BK_GfxTexture *texture_b = (BK_GfxTexture *)dummy_b;
  BK_Aabb unit = bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f));

  bk__draw_reset();
  // Color, layer, antialias and shape type all change -- none of them may split.
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 1.0f);
  bk_draw_pop_color();
  bk_draw_push_antialias(0.0f);
  bk_draw_line(bk_v2(-1.0f, 0.0f), bk_v2(1.0f, 0.0f), 1.0f);
  bk_draw_pop_antialias();

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 1);
  REQUIRE(packed.batches[0].first == 0);
  REQUIRE(packed.batches[0].count == 3);

  bk__draw_reset();
  bk_draw_texture(texture_a, unit, unit);
  bk_draw_texture(texture_a, unit, unit); // same texture -- no split
  bk_draw_texture(texture_b, unit, unit); // different texture -- splits
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 2);
  REQUIRE(packed.batches[0].count == 2);
  REQUIRE(packed.batches[1].count == 1);
  // Not trivially true like batches[0].first == 0 above: batches[1].first == 2 only
  // holds if it comes from batch 0's actual command count, not (say) the batch's own
  // loop index (which would be 1 here) -- the same distinction bk__draw_collate's
  // batch-base uniform depends on getting right (see DEVIATIONS.md).
  REQUIRE(packed.batches[1].first == 2);
  REQUIRE(packed.batches[0].texture == texture_a);
  REQUIRE(packed.batches[1].texture == texture_b);

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push_scissor((BK_Rect){.x = 0, .y = 0, .width = 10, .height = 10});
  bk_draw_box_fill(unit, 0.0f); // different scissor -- splits
  bk_draw_pop_scissor();
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.batch_count == 2);
}

static void test_matrix_palette_dedupes_identical_transforms(void) {
  BK_Aabb unit = bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f));

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_box_fill(unit, 0.0f);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  // A frame that never touches the camera emits exactly one palette entry, and every
  // command selects it.
  REQUIRE(packed.cmds[0].meta[3] == packed.cmds[1].meta[3]);
  REQUIRE(packed.cmds[1].meta[3] == packed.cmds[2].meta[3]);

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_push();
  bk_draw_translate(bk_v2(50.0f, 0.0f));
  bk_draw_box_fill(unit, 0.0f);
  bk_draw_pop();
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmds[0].meta[3] != packed.cmds[1].meta[3]);
}

static void test_payload_offsets_match_each_type(void) {
  BK_Aabb unit = bk_aabb(bk_v2(-2.0f, -3.0f), bk_v2(4.0f, 5.0f));

  bk__draw_reset();
  bk_draw_box_fill(unit, 0.0f);                                  // 1 payload vec4
  bk_draw_tri_fill(bk_v2(0, 0), bk_v2(1, 0), bk_v2(0, 1), 0.0f); // 2 payload vec4s

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));

  // The box's payload holds centre then half-extents.
  u32 box_offset = packed.cmds[0].meta[2];
  REQUIRE_NEAR(packed.payload[box_offset].x, 1.0f, 1e-5); // centre x = (-2+4)/2
  REQUIRE_NEAR(packed.payload[box_offset].y, 1.0f, 1e-5); // centre y = (-3+5)/2
  REQUIRE_NEAR(packed.payload[box_offset].z, 3.0f, 1e-5); // half-extent x
  REQUIRE_NEAR(packed.payload[box_offset].w, 4.0f, 1e-5); // half-extent y

  // The triangle's payload starts after the box's single vec4.
  u32 tri_offset = packed.cmds[1].meta[2];
  REQUIRE(tri_offset == box_offset + 1);
  REQUIRE(packed.payload_count >= (i32)(tri_offset + 2));
}

static void test_fill_flag_is_not_half_stroke(void) {
  bk__draw_reset();

  // The line is the case that actually discriminates: fill = true with a nonzero
  // half_stroke (a capsule). Regressing the packer to branch on half_stroke == 0
  // instead of .fill would render it hollow, with no crash and no log.
  bk_draw_line(bk_v2(-1.0f, 0.0f), bk_v2(1.0f, 0.0f), 2.0f);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 1);
  REQUIRE(packed.cmds[0].misc[0] == 1.0f);
  REQUIRE_NEAR(packed.cmds[0].shape[1], 1.0f, 1e-5); // half_stroke = thickness / 2, nonzero
}

static void test_arrow_shaft_radius_lands_in_payload_not_half_stroke(void) {
  bk__draw_reset();

  // Unlike the line, bk_draw_arrow never assigns half_stroke -- it stays zero from
  // s_record's designated initializer. The shaft radius travels in shape[2].x instead,
  // alongside head_width in shape[2].y, and s_write_shape_payload packs that pair into
  // the payload vec4 right after (a, b). Task 4's shader must read the arrow's stroke
  // width from there, not from BK_DrawCmd.shape[1] -- a mismatch that would drop the
  // command buffer at draw time with nothing logged, per this project's standing
  // silent-failure hazard.
  bk_draw_arrow(bk_v2(-1.0f, 0.0f), bk_v2(1.0f, 0.0f), 2.0f, 4.0f);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 1);
  REQUIRE(packed.cmds[0].misc[0] == 1.0f);  // filled shaft+head SDF, not a stroked outline
  REQUIRE(packed.cmds[0].shape[1] == 0.0f); // half_stroke is unused for ARROW

  u32 offset = packed.cmds[0].meta[2];
  REQUIRE_NEAR(packed.payload[offset + 1].x, 1.0f, 1e-5); // shaft_radius = thickness / 2
  REQUIRE_NEAR(packed.payload[offset + 1].y, 4.0f, 1e-5); // head_width
}

/// Unpacks one IEEE-754 binary16 value (in the low 16 bits of half) back to f32 -- this
/// test's own inverse of the packer's s_half_bits, used to verify the round trip through
/// BK_DrawCmd.meta[1]/misc[1] rather than trusting the encoding by inspection alone.
static f32 s_unpack_half(u16 half) {
  u32 sign = (u32)(half & 0x8000u) << 16;
  u32 exponent = (half >> 10) & 0x1Fu;
  u32 mantissa = half & 0x3FFu;
  u32 bits = exponent == 0 ? sign : sign | ((exponent + 112u) << 23) | (mantissa << 13);

  union {
    u32 bits;
    f32 value;
  } convert = {.bits = bits};

  return convert.value;
}

static void test_color_half4_round_trips_through_meta_and_misc(void) {
  bk__draw_reset();
  bk_draw_push_color((BK_Color){0.25f, 0.5f, 0.75f, 0.5f});
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == 1);

  // Premultiplied at record time (bk_color_premultiply), so this is what the packer
  // actually encoded -- every component here is exactly representable in binary16, so
  // the round trip is bit-exact, not just within tolerance.
  BK_Color expected = bk_color_premultiply((BK_Color){0.25f, 0.5f, 0.75f, 0.5f});

  u32 color_rg = packed.cmds[0].meta[1];
  REQUIRE_NEAR(s_unpack_half((u16)(color_rg & 0xFFFFu)), expected.r, 1e-6);
  REQUIRE_NEAR(s_unpack_half((u16)(color_rg >> 16)), expected.g, 1e-6);

  union {
    f32 value;
    u32 bits;
  } convert = {.value = packed.cmds[0].misc[1]}; // reinterpret, per BK_DrawCmd's contract

  u32 color_ba = convert.bits;
  REQUIRE_NEAR(s_unpack_half((u16)(color_ba & 0xFFFFu)), expected.b, 1e-6);
  REQUIRE_NEAR(s_unpack_half((u16)(color_ba >> 16)), expected.a, 1e-6);
}

static void test_records_survive_an_arena_growth(void) {
  // Both bk_draw record paths hold frame-arena pointers across later bk_frame_alloc
  // calls: s_record writes the new record through the *previous* record's next field,
  // and bk__draw_pack captures the chain head before allocating its four output arrays.
  // Both are writes through freed memory if growing the arena moves its backing block,
  // which SDL_realloc is free to do. ASan aborts on it; a Release build corrupts the
  // first frame that crosses the capacity and then runs clean, which reads as a glitch.
  //
  // 60000 records x sizeof(BK_DrawGeom) (~136 bytes) is ~8MB, comfortably past the
  // arena's 4MB default capacity, so the chain alone forces at least one growth. That
  // capacity is private to bk_app.c and invisible here -- raising it would defang this
  // test. Every box stays on the default layer 0: bk__draw_pack's stable sort is an
  // insertion sort, linear on equal layers and quadratic on varied ones.
  constexpr i32 box_count = 60000;

  bk__draw_reset();
  bk__arena_reset();

  for (i32 i = 0; i < box_count; i++) {
    f32 x = (f32)i;
    bk_draw_box_fill(bk_aabb(bk_v2(x, 0.0f), bk_v2(x + 2.0f, 2.0f)), 0.0f);
  }
  REQUIRE(bk__draw_get_geom_count() == box_count);

  BK_DrawPacked packed = {0};
  REQUIRE(bk__draw_pack(&packed, 640, 480));
  REQUIRE(packed.cmd_count == box_count);

  // Box i's centre x is i + 1, exactly representable in f32 across this range. Checking
  // the last record as well as the first is the point: a chain corrupted partway through
  // the frame loses everything recorded after the growth.
  REQUIRE_NEAR(packed.payload[packed.cmds[0].meta[2]].x, 1.0f, 1e-3);
  REQUIRE_NEAR(packed.payload[packed.cmds[box_count - 1].meta[2]].x, (f32)box_count, 1e-3);
}

static void test_shutdown_clears_the_record_chain(void) {
  // bk__iterate returns early when update returns non-BK_CONTINUE, skipping collate and
  // therefore bk__draw_reset. An app that records from update and quits on the same tick
  // would otherwise leave the chain pointing into memory bk__arena_free is about to
  // release, and a second bk_run in the same process would walk it. Needs no device:
  // every GPU resource shutdown destroys is nullptr here, and all four destroy entry
  // points return on nullptr.
  bk__draw_reset();
  bk_draw_box_fill(bk_aabb(bk_v2(-1.0f, -1.0f), bk_v2(1.0f, 1.0f)), 0.0f);
  REQUIRE(bk__draw_get_geom_count() == 1);

  bk__draw_shutdown();

  REQUIRE(bk__draw_get_geom_count() == 0);
  REQUIRE(bk__draw_get_geom(0) == nullptr);
}

static void test_embedded_shader_bytecode_is_present(void) {
  // Guards the CMake generator: a mis-wired embed step yields empty arrays, and the
  // pipeline would then fail at init with a far less obvious message.
  REQUIRE(bk__draw_vertex_spv_size > 0);
  REQUIRE(bk__draw_fragment_spv_size > 0);
  // SPIR-V's magic number, little-endian.
  REQUIRE(bk__draw_vertex_spv[0] == 0x03);
  REQUIRE(bk__draw_vertex_spv[1] == 0x02);
  REQUIRE(bk__draw_vertex_spv[2] == 0x23);
  REQUIRE(bk__draw_vertex_spv[3] == 0x07);
}

int main(void) {
  test_color_stack_pushes_pops_and_peeks();
  test_records_snapshot_their_own_state();
  test_transform_composes_and_restores();
  test_records_capture_the_camera_transform();
  test_singular_transform_is_culled();
  test_reset_clears_records_and_stacks();
  test_packed_cmd_matches_the_shader_layout();
  test_layers_sort_above_record_order();
  test_equal_layers_keep_record_order();
  test_batches_split_on_texture_and_scissor_only();
  test_matrix_palette_dedupes_identical_transforms();
  test_payload_offsets_match_each_type();
  test_fill_flag_is_not_half_stroke();
  test_arrow_shaft_radius_lands_in_payload_not_half_stroke();
  test_color_half4_round_trips_through_meta_and_misc();
  test_records_survive_an_arena_growth();
  test_shutdown_clears_the_record_chain();
  test_embedded_shader_bytecode_is_present();
  printf("test_draw: OK\n");
  return 0;
}
