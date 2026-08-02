#include "bk_test.h"

#include <bielik/bk_app.h>
#include <bielik/bk_draw.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_math.h>

#include <SDL3/SDL.h>

#include <stdio.h>

// GPU-dependent bk_draw tests: end-to-end replay through a real window and device,
// exercising bk__draw_init/bk__draw_collate/bk__draw_shutdown as bk_app.c wires them in
// (see test_gfx_drawlist_gpu.c, which this file's harness is modeled on). Runs in CI's
// allow-failure GPU group, same as that file, for the same reason (no DXIL variant
// exists for Windows -- see DEVIATIONS.md).

static void (*s_render_fn)(void *, const BK_FrameInfo *) = nullptr;
static char s_capture_path[512];
static int s_frames = 0;

static BK_Result s_test_update(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  s_frames++;
  return s_frames >= 3 ? BK_DONE : BK_CONTINUE;
}

static void s_test_render(void *state, const BK_FrameInfo *frame) {
  s_render_fn(state, frame);
  bk_gfx_request_capture(s_capture_path);
}

// Boots a one-frame app with the given init/render/quit callbacks, captures the frame,
// and hands back the RGBA32 surface. Returns nullptr if no capture was produced --
// callers treat that as a skip, not a failure. Caller destroys the surface. init/quit
// may be nullptr for a case that needs no setup beyond bk_draw_* calls in render.
static SDL_Surface *s_render_one_frame_full(BK_Result (*init)(void **, int, char **),
                                            void (*render)(void *, const BK_FrameInfo *),
                                            void (*quit)(void *, BK_Result)) {
  s_render_fn = render;
  s_frames = 0;

  const char *base_path = SDL_GetBasePath();
  REQUIRE(base_path != nullptr);
  SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_draw_gpu_output.bmp", base_path);
  SDL_RemovePath(s_capture_path);

  BK_AppDesc desc = {
      .window = {.title = "test_draw_gpu", .width = 1280, .height = 720},
      .time = {.tick_hz = 60},
      .init = init,
      .update = s_test_update,
      .render = s_test_render,
      .quit = quit,
  };
  bk_run(&desc, 0, nullptr);

  SDL_Surface *surface = SDL_LoadBMP(s_capture_path);
  if (surface == nullptr) {
    return nullptr;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  REQUIRE(rgba != nullptr);
  SDL_RemovePath(s_capture_path);
  return rgba;
}

// Boots a one-frame app with the given render callback, captures the frame, and hands
// back the RGBA32 surface. Returns nullptr if no capture was produced -- callers treat
// that as a skip, not a failure. Caller destroys the surface.
static SDL_Surface *s_render_one_frame(void (*render)(void *, const BK_FrameInfo *)) {
  return s_render_one_frame_full(nullptr, render, nullptr);
}

// Byte offset of the pixel at (x, y) in a converted surface.
static const u8 *s_pixel_at(const SDL_Surface *surface, int px, int py) {
  return (const u8 *)surface->pixels + (usize)py * (usize)surface->pitch + (usize)px * 4;
}

// A filled red box centred in the window: the centre pixel is red, a corner well
// outside it is the clear colour. Probes specific pixels rather than hashing the
// frame -- SDF antialiasing does not hash-match across Metal and Vulkan.
static void app_render_box(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f)), 0.0f);
  bk_draw_pop_color();
}

static void test_filled_box_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_box);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping filled box\n");
    return;
  }

  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200); // red
  REQUIRE(centre[1] < 55);
  REQUIRE(centre[2] < 55);

  // Well outside the 128x128 box, so this is the clear colour.
  const u8 *corner = s_pixel_at(frame, 4, 4);
  REQUIRE(corner[0] < 55);

  SDL_DestroySurface(frame);
}

// F1 regression (Task 5 review): an app-set viewport must not leak into bk_draw's own
// batches. bk_gfx's bound state is sticky and only cleared inside flush, which runs
// after collate -- without an explicit per-batch reset, a viewport an app set before
// its render callback returned would apply to every batch bk_draw emits too. This is
// the review's own repro: a viewport covering only the window's top-left quarter,
// set immediately before a box drawn at the world origin.
static void app_render_box_with_viewport_set(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_gfx_set_viewport((BK_Rect){.x = 0, .y = 0, .width = 640, .height = 360});
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f)), 0.0f);
  bk_draw_pop_color();
}

static void test_app_viewport_does_not_leak_into_batches(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_box_with_viewport_set);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping viewport-leak regression\n");
    return;
  }

  // The box is recorded at the world origin, which the default projection maps to the
  // FULL target's centre -- regardless of the {0,0,640,360} viewport the app set before
  // render returned. Before the F1 fix, the leaked viewport moved this to (320,180),
  // a quarter of the way into the 1280x720 window, instead.
  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200);
  REQUIRE(centre[1] < 55);
  REQUIRE(centre[2] < 55);

  SDL_DestroySurface(frame);
}

// Isolating probe: does world +y (bk_m3x2_ortho's documented "y up" contract) actually
// land in the upper rows of a captured BMP? Nothing else in this repo's GPU tests pins
// that down -- test_filled_box_draws above is y-symmetric, and test_gfx_drawlist_gpu.c's
// checks are x-only or symmetric too. bk_draw_texture's V-flip (s_write_shape_payload's
// TEXTURE case) is derived by assuming yes; this is checked before trusting that.
static void app_render_box_upper(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bk_aabb(bk_v2(-64.0f, 32.0f), bk_v2(64.0f, 96.0f)), 0.0f);
  bk_draw_pop_color();
}

static void test_positive_y_is_up(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_box_upper);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping y-orientation probe\n");
    return;
  }

  // The box spans world y in [32, 96] -- entirely above the origin. If +y is up and the
  // capture's row 0 is the image's top (both true per bk_m3x2_ortho's and
  // SDL_Surface's documented conventions), it lands in the image's UPPER half.
  const u8 *upper = s_pixel_at(frame, frame->w / 2, frame->h / 2 - 64);
  REQUIRE(upper[0] > 200);
  REQUIRE(upper[1] < 55);

  // The mirror position (world y = -64) is outside the box -- still the clear colour.
  const u8 *lower_mirror = s_pixel_at(frame, frame->w / 2, frame->h / 2 + 64);
  REQUIRE(lower_mirror[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Textured quad: proves bk_draw_texture's src_px -> UV normalisation and V-flip
// (s_write_shape_payload's TEXTURE case) against a 2x2 checkerboard.
// ---------------------------------------------------------------------------

static BK_GfxTexture *s_quadrant_texture = nullptr;

static BK_Result s_quadrant_texture_init(void **state, int argc, char **argv) {
  (void)state;
  (void)argc;
  (void)argv;
  // Same row-major, top-to-bottom layout test_gfx_texture.c's checkerboard already
  // proves works (texel row 0 is the texture's top row, matching
  // SDL_UploadToGPUTexture's pixels_per_row/rows_per_layer convention).
  u8 checkerboard[2][2][4] = {
      {{255, 0, 0, 255}, {0, 255, 0, 255}  }, // row 0 (top texel row):    red,  green
      {{0, 0, 255, 255}, {255, 255, 0, 255}}, // row 1 (bottom texel row): blue, yellow
  };
  s_quadrant_texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
  REQUIRE(s_quadrant_texture != nullptr);
  REQUIRE(bk_gfx_texture_upload(s_quadrant_texture, checkerboard));
  return BK_CONTINUE;
}

static void s_quadrant_texture_quit(void *state, BK_Result result) {
  (void)state;
  (void)result;
  bk_gfx_texture_destroy(s_quadrant_texture);
  s_quadrant_texture = nullptr;
}

static void app_render_texture_quadrants(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_texture(s_quadrant_texture, bk_aabb(bk_v2(0.0f, 0.0f), bk_v2(2.0f, 2.0f)),
                  bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f)));
}

static void test_texture_quadrants_v_flip_is_correct(void) {
  SDL_Surface *frame = s_render_one_frame_full(
      s_quadrant_texture_init, app_render_texture_quadrants, s_quadrant_texture_quit);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping texture quadrant probe\n");
    return;
  }

  // 25%/75% of the 128px dst span lands on texel centres (u/v = 0.25/0.75), where the
  // default LINEAR sampler's weight is ~100% one texel -- probing nearer the middle
  // would blend two texels and prove nothing.
  constexpr int off = 32;
  constexpr int tol = 40;

  const u8 *top_left = s_pixel_at(frame, frame->w / 2 - off, frame->h / 2 - off);
  const u8 *top_right = s_pixel_at(frame, frame->w / 2 + off, frame->h / 2 - off);
  const u8 *bottom_left = s_pixel_at(frame, frame->w / 2 - off, frame->h / 2 + off);
  const u8 *bottom_right = s_pixel_at(frame, frame->w / 2 + off, frame->h / 2 + off);

  // Visually top-left of the drawn quad is world top-left (dst.min.x, dst.max.y --
  // +y is up and row 0 is the image's top, per test_positive_y_is_up above), which the
  // V-flip maps to the texture's own top-left texel: red.
  REQUIRE(top_left[0] > 255 - tol && top_left[1] < tol && top_left[2] < tol);          // red
  REQUIRE(top_right[1] > 255 - tol && top_right[0] < tol && top_right[2] < tol);       // green
  REQUIRE(bottom_left[2] > 255 - tol && bottom_left[0] < tol && bottom_left[1] < tol); // blue
  REQUIRE(bottom_right[0] > 255 - tol && bottom_right[1] > 255 - tol &&
          bottom_right[2] < tol); // yellow

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Filled circle
// ---------------------------------------------------------------------------

static void app_render_circle_fill(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_circle_fill(bk_v2(0.0f, 0.0f), 64.0f);
  bk_draw_pop_color();
}

static void test_filled_circle_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_circle_fill);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping filled circle\n");
    return;
  }

  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200);
  REQUIRE(centre[1] < 55);

  // World (0, 100) is outside the 64-radius circle.
  const u8 *outside = s_pixel_at(frame, frame->w / 2, frame->h / 2 - 100);
  REQUIRE(outside[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Stroked circle -- hollow, centre must be clear
// ---------------------------------------------------------------------------

static void app_render_circle_stroke(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_circle(bk_v2(0.0f, 0.0f), 64.0f, 8.0f);
  bk_draw_pop_color();
}

static void test_stroked_circle_is_hollow(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_circle_stroke);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping stroked circle\n");
    return;
  }

  // The centre is inside the ring's hole -- if this were filled, half_stroke
  // reached the shader without the fill/stroke branch taking effect.
  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] < 55);

  // World (64, 0) sits on the 64-radius ring.
  const u8 *ring = s_pixel_at(frame, frame->w / 2 + 64, frame->h / 2);
  REQUIRE(ring[0] > 200);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Line -- a filled capsule, not a stroked outline
// ---------------------------------------------------------------------------

static void app_render_line(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_line(bk_v2(-64.0f, 0.0f), bk_v2(64.0f, 0.0f), 8.0f);
  bk_draw_pop_color();
}

static void test_line_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_line);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping line\n");
    return;
  }

  const u8 *on_segment = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(on_segment[0] > 200);

  // 40px above the segment, well outside its 4px half-thickness.
  const u8 *above = s_pixel_at(frame, frame->w / 2, frame->h / 2 - 40);
  REQUIRE(above[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Filled triangle
// ---------------------------------------------------------------------------

static void app_render_tri_fill(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_tri_fill(bk_v2(0.0f, 64.0f), bk_v2(-64.0f, -64.0f), bk_v2(64.0f, -64.0f), 0.0f);
  bk_draw_pop_color();
}

static void test_filled_tri_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_tri_fill);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping filled triangle\n");
    return;
  }

  // Centroid: ((0 - 64 + 64) / 3, (64 - 64 - 64) / 3) = (0, -21.33).
  const u8 *centroid = s_pixel_at(frame, frame->w / 2, frame->h / 2 + 21);
  REQUIRE(centroid[0] > 200);

  // The AABB's top-right corner (64, 64) -- outside the triangle, which only
  // touches that edge of the box at the apex (0, 64).
  const u8 *corner = s_pixel_at(frame, frame->w / 2 + 64, frame->h / 2 - 64);
  REQUIRE(corner[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Arrow -- shaft + head as one SDF, half_stroke left at zero
// ---------------------------------------------------------------------------

static void app_render_arrow(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_arrow(bk_v2(-64.0f, 0.0f), bk_v2(64.0f, 0.0f), 6.0f, 20.0f);
  bk_draw_pop_color();
}

static void test_arrow_draws(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_arrow);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping arrow\n");
    return;
  }

  const u8 *shaft = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(shaft[0] > 200);

  // Inside the head's silhouette only -- at x=48 the head triangle's half-height is
  // 20 * (1 - (48-44)/(64-44)) = 16, so y=12 is inside it, while the shaft segment
  // alone (radius 3) is ~9.65 units away at this point: filled iff the head is drawn,
  // not just the shaft. Catches a zeroed/malformed head_width that a shaft-only or
  // above-the-head probe cannot.
  const u8 *head = s_pixel_at(frame, frame->w / 2 + 48, frame->h / 2 - 12);
  REQUIRE(head[0] > 200);

  // Well above the head, whose triangular base tops out at world y = 20.
  const u8 *above_head = s_pixel_at(frame, frame->w / 2 + 50, frame->h / 2 - 80);
  REQUIRE(above_head[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Rounded box -- radius cuts the corner
// ---------------------------------------------------------------------------

static void app_render_rounded_box(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f)), 24.0f);
  bk_draw_pop_color();
}

static void test_rounded_box_corner_is_cut(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_rounded_box);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping rounded box\n");
    return;
  }

  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200);

  // The box's exact corner -- a 24px radius cuts it well clear of the fill.
  const u8 *corner = s_pixel_at(frame, frame->w / 2 + 64, frame->h / 2 - 64);
  REQUIRE(corner[0] < 55);

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Layer reordering: a lower layer paints first, so it ends up underneath
// ---------------------------------------------------------------------------

static void app_render_layers(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  BK_Aabb bb = bk_aabb(bk_v2(-64.0f, -64.0f), bk_v2(64.0f, 64.0f));

  bk_draw_push_layer(5);
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  bk_draw_box_fill(bb, 0.0f);
  bk_draw_pop_color();
  bk_draw_pop_layer();

  // Recorded second but at a lower layer, so it paints first -- underneath.
  bk_draw_push_color((BK_Color){0.0f, 0.0f, 1.0f, 1.0f});
  bk_draw_box_fill(bb, 0.0f);
  bk_draw_pop_color();
}

static void test_layers_reorder_paint_order(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_layers);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping layer reorder\n");
    return;
  }

  const u8 *centre = s_pixel_at(frame, frame->w / 2, frame->h / 2);
  REQUIRE(centre[0] > 200); // red on top
  REQUIRE(centre[2] < 55);  // not blue

  SDL_DestroySurface(frame);
}

// ---------------------------------------------------------------------------
// Scissor clipping
// ---------------------------------------------------------------------------

static void app_render_scissor(void *state, const BK_FrameInfo *frame) {
  (void)state;
  (void)frame;
  bk_draw_push_scissor((BK_Rect){.x = 0, .y = 0, .width = 640, .height = 720});
  bk_draw_push_color((BK_Color){1.0f, 0.0f, 0.0f, 1.0f});
  // Covers the entire 1280x720 window; the scissor is what clips it.
  bk_draw_box_fill(bk_aabb(bk_v2(-700.0f, -400.0f), bk_v2(700.0f, 400.0f)), 0.0f);
  bk_draw_pop_color();
  bk_draw_pop_scissor();
}

static void test_scissor_clips(void) {
  SDL_Surface *frame = s_render_one_frame(app_render_scissor);
  if (frame == nullptr) {
    printf("test_draw_gpu: no capture produced, skipping scissor clip\n");
    return;
  }

  const u8 *left = s_pixel_at(frame, 100, frame->h / 2);
  REQUIRE(left[0] > 200);

  const u8 *right = s_pixel_at(frame, 1200, frame->h / 2);
  REQUIRE(right[0] < 55);

  SDL_DestroySurface(frame);
}

int main(void) {
  test_filled_box_draws();
  test_app_viewport_does_not_leak_into_batches();
  test_positive_y_is_up();
  test_texture_quadrants_v_flip_is_correct();
  test_filled_circle_draws();
  test_stroked_circle_is_hollow();
  test_line_draws();
  test_filled_tri_draws();
  test_arrow_draws();
  test_rounded_box_corner_is_cut();
  test_layers_reorder_paint_order();
  test_scissor_clips();
  printf("test_draw_gpu: OK\n");
  return 0;
}
