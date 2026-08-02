// 08_draw -- every bk_draw shape and state-stack feature in one frame. Unlike every other
// sample in this directory, there is no shader loading and no BK_GfxPipeline: bk_draw owns
// its own pipelines, and its shader bytecode is compiled straight into the library (Task 4's
// bk_draw_shaders.h). app_render below calls straight into bk_draw_* with nothing to set up
// beyond the one texture bk_draw_texture needs -- that contrast with 07_instanced (and every
// sample before it) is the point of this one.

#include <bielik/bk_draw.h>
#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_main.h>
#include <bielik/bk_math.h>

typedef struct AppState {
  BK_GfxTexture *texture;
  int frame_count;
  int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

// A 2x2 texture with one distinct color per quadrant, so a V-flip regression would be
// visible by eye rather than hiding behind a symmetric checkerboard. Row 0 is the texture's
// top texel row (SDL_UploadToGPUTexture's row-major, top-to-bottom convention).
static BK_Result s_create_texture(void) {
  u8 quadrants[2][2][4] = {
      {{255, 0, 0, 255}, {0, 255, 0, 255}  }, // row 0 (top):    red,  green
      {{0, 0, 255, 255}, {255, 255, 0, 255}}, // row 1 (bottom): blue, yellow
  };
  s_state.texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_SAMPLER, 2, 2);
  if (s_state.texture == nullptr || !bk_gfx_texture_upload(s_state.texture, quadrants)) {
    return BK_FAIL;
  }
  return BK_CONTINUE;
}

static BK_Result app_init(void **state, int argc, char **argv) {
  *state = &s_state;

  for (int i = 1; i < argc; i++) {
    if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      s_state.frame_limit = SDL_atoi(argv[i + 1]);
    }
  }

  if (s_create_texture() != BK_CONTINUE) {
    return BK_FAIL;
  }

  bk_gfx_set_clear_color((BK_Color){0.06f, 0.07f, 0.10f, 1.0f});
  return BK_CONTINUE;
}

static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
  AppState *app = state;
  (void)frame;
  app->frame_count++;
  if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

// The default projection (bk_m3x2_ortho over the render target) puts world (0,0) at the
// window's centre, +y up, one world unit per pixel -- every position below is in that space.
static void app_render(void *state, const BK_FrameInfo *frame) {
  AppState *app = state;

  // -------------------------------------------------------------------------------------
  // Shape gallery: every bk_draw_* shape in a row, filled and stroked where both exist,
  // plus the rounded-box radius variant and a textured quad. One color for the row (popped
  // before the texture, so the texture's own quadrant colors show through untinted).
  // -------------------------------------------------------------------------------------
  constexpr f32 GALLERY_Y = 220.0f;
  constexpr f32 GALLERY_SIZE = 36.0f; // shared half-extent/radius
  constexpr f32 GALLERY_SPACING = 120.0f;
  f32 x = -540.0f;

  bk_draw_push_color((BK_Color){0.35f, 0.85f, 0.90f, 1.0f});

  // bk_draw_box_fill: sharp-cornered fill.
  bk_draw_box_fill(bk_aabb_from_center(bk_v2(x, GALLERY_Y), bk_v2(GALLERY_SIZE, GALLERY_SIZE)),
                   0.0f);
  x += GALLERY_SPACING;

  // bk_draw_box: sharp-cornered stroke.
  bk_draw_box(bk_aabb_from_center(bk_v2(x, GALLERY_Y), bk_v2(GALLERY_SIZE, GALLERY_SIZE)), 6.0f,
              0.0f);
  x += GALLERY_SPACING;

  // bk_draw_box_fill again, this time with radius > 0: the rounded variant.
  bk_draw_box_fill(bk_aabb_from_center(bk_v2(x, GALLERY_Y), bk_v2(GALLERY_SIZE, GALLERY_SIZE)),
                   16.0f);
  x += GALLERY_SPACING;

  // bk_draw_circle_fill.
  bk_draw_circle_fill(bk_v2(x, GALLERY_Y), GALLERY_SIZE);
  x += GALLERY_SPACING;

  // bk_draw_circle: stroked, hollow.
  bk_draw_circle(bk_v2(x, GALLERY_Y), GALLERY_SIZE, 6.0f);
  x += GALLERY_SPACING;

  // bk_draw_line: a filled capsule with round caps, not a stroked outline.
  bk_draw_line(bk_v2(x - GALLERY_SIZE, GALLERY_Y), bk_v2(x + GALLERY_SIZE, GALLERY_Y), 12.0f);
  x += GALLERY_SPACING;

  // bk_draw_tri_fill.
  bk_draw_tri_fill(bk_v2(x, GALLERY_Y + GALLERY_SIZE),
                   bk_v2(x - GALLERY_SIZE, GALLERY_Y - GALLERY_SIZE),
                   bk_v2(x + GALLERY_SIZE, GALLERY_Y - GALLERY_SIZE), 0.0f);
  x += GALLERY_SPACING;

  // bk_draw_tri: stroked.
  bk_draw_tri(bk_v2(x, GALLERY_Y + GALLERY_SIZE), bk_v2(x - GALLERY_SIZE, GALLERY_Y - GALLERY_SIZE),
              bk_v2(x + GALLERY_SIZE, GALLERY_Y - GALLERY_SIZE), 6.0f, 0.0f);
  x += GALLERY_SPACING;

  // bk_draw_arrow: shaft + head as one SDF. head_width (24) well above thickness (6) so
  // the head reads clearly instead of blending into the shaft.
  bk_draw_arrow(bk_v2(x - GALLERY_SIZE, GALLERY_Y), bk_v2(x + GALLERY_SIZE, GALLERY_Y), 6.0f,
                24.0f);
  x += GALLERY_SPACING;

  bk_draw_pop_color();

  // bk_draw_texture: the quadrant texture drawn whole, untinted.
  bk_draw_texture(app->texture, bk_aabb(bk_v2(0.0f, 0.0f), bk_v2(2.0f, 2.0f)),
                  bk_aabb_from_center(bk_v2(x, GALLERY_Y), bk_v2(GALLERY_SIZE, GALLERY_SIZE)));

  // -------------------------------------------------------------------------------------
  // Camera stack doing visible work: bk_draw_push/bk_draw_translate/bk_draw_rotate/
  // bk_draw_pop establishes a rotating local origin, and a box + arrow recorded relative
  // to that origin sweep together -- proof the transform is live rather than identity.
  // -------------------------------------------------------------------------------------
  f32 spin = (f32)frame->real_time * 1.2f;
  bk_draw_push();
  bk_draw_translate(bk_v2(-420.0f, -60.0f));
  bk_draw_rotate(spin);
  bk_draw_push_color((BK_Color){0.85f, 0.55f, 0.95f, 1.0f});
  bk_draw_box_fill(bk_aabb_from_center(bk_v2_zero(), bk_v2(28.0f, 28.0f)), 6.0f);
  bk_draw_arrow(bk_v2_zero(), bk_v2(70.0f, 0.0f), 6.0f, 24.0f);
  bk_draw_pop_color();
  bk_draw_pop();

  // -------------------------------------------------------------------------------------
  // Layer inversion: recorded first but painted last. Without an explicit layer, record
  // order alone would put the blue box (recorded second) on top; the red box's higher
  // layer overrides that, so it paints over the blue box despite coming first.
  // -------------------------------------------------------------------------------------
  bk_draw_push_layer(5);
  bk_draw_push_color((BK_Color){1.0f, 0.25f, 0.25f, 1.0f});
  bk_draw_box_fill(bk_aabb_from_center(bk_v2(-20.0f, -140.0f), bk_v2(50.0f, 50.0f)), 0.0f);
  bk_draw_pop_color();
  bk_draw_pop_layer();

  bk_draw_push_color((BK_Color){0.25f, 0.45f, 1.0f, 1.0f});
  bk_draw_box_fill(bk_aabb_from_center(bk_v2(40.0f, -180.0f), bk_v2(50.0f, 50.0f)), 0.0f);
  bk_draw_pop_color();

  // -------------------------------------------------------------------------------------
  // Antialias on vs off, side by side: the same circle, once under the default 1px AA
  // band and once with it disabled (0.0f) -- the pixel-art path. A circle's curved edge
  // makes the difference obvious where an axis-aligned box's edge would not.
  // -------------------------------------------------------------------------------------
  bk_draw_push_color((BK_Color){0.90f, 0.90f, 0.20f, 1.0f});
  bk_draw_push_antialias(1.0f);
  bk_draw_circle_fill(bk_v2(-150.0f, -300.0f), 50.0f);
  bk_draw_pop_antialias();

  bk_draw_push_antialias(0.0f);
  bk_draw_circle_fill(bk_v2(50.0f, -300.0f), 50.0f);
  bk_draw_pop_antialias();
  bk_draw_pop_color();
}

static BK_Result app_event(void *state, const SDL_Event *event) {
  (void)state;
  if (event->type == SDL_EVENT_QUIT) {
    return BK_DONE;
  }
  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
  AppState *app = state;
  (void)result;
  bk_gfx_texture_destroy(app->texture);
  app->texture = nullptr;
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .window = {.title = "08_draw", .width = 1280, .height = 720},
      .init = app_init,
      .update = app_update,
      .render = app_render,
      .event = app_event,
      .quit = app_quit,
  };
  return bk_run(&desc, argc, argv);
}
#else
BK_APP(.window = {.title = "08_draw", .width = 1280, .height = 720}, .init = app_init,
       .update = app_update, .render = app_render, .event = app_event, .quit = app_quit, )
#endif
