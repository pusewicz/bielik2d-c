#include "bk_test.h"
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

int main(void) {
  test_color_stack_pushes_pops_and_peeks();
  test_records_snapshot_their_own_state();
  test_transform_composes_and_restores();
  test_records_capture_the_camera_transform();
  test_singular_transform_is_culled();
  test_reset_clears_records_and_stacks();
  printf("test_draw: OK\n");
  return 0;
}
