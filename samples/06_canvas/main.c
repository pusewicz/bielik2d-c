// 06_canvas -- the smallest use of bk_gfx_canvas + depth-stencil together: two
// overlapping triangles at different depths are drawn into a fixed-resolution 160x90
// canvas with a depth attachment, then blitted (NEAREST) onto a resizable window --
// the retro/pixel-art path bk_gfx_bind_canvas exists for. The near (red) triangle
// must stay in front of the far (blue) one in the overlap region no matter how the
// window is resized, which is what a real depth test looks like end to end (the
// golden-image test in tests/test_gfx_canvas.c proves the same thing headlessly).
// Built once, using the BK_APP entry-point macro like 01_clear/03_triangle.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_main.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex {
  float position[3];
  uint8_t color[4];
} Vertex;

constexpr int CANVAS_W = 160;
constexpr int CANVAS_H = 90;

typedef struct AppState {
  BK_GfxCanvas *canvas;
  BK_GfxPipeline *pipeline;
  BK_GfxBuffer *vertex_buffer;
  int frame_count;
  int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, size_t *out_size) {
  const char *base_path = SDL_GetBasePath();
  if (base_path == nullptr) {
    SDL_Log("BK: SDL_GetBasePath failed: %s", SDL_GetError());
    return nullptr;
  }

  char path[512];
  SDL_snprintf(path, sizeof path, "%sshaders/%s", base_path, relative_path);
  void *data = SDL_LoadFile(path, out_size);
  if (data == nullptr) {
    SDL_Log("BK: failed to load shader file %s: %s", path, SDL_GetError());
  }
  return data;
}

static BK_GfxShaderDesc s_load_depth_tri_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "depth_tri.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "depth_tri.%s.msl", stage);

  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

// init: create the canvas (with a depth attachment), build a depth-testing pipeline
// targeting the canvas's format, and upload two overlapping triangles at different
// depths -- all up front, the same way 04_textured_quad builds its quad in init.
static BK_Result app_init(void **state, int argc, char **argv) {
  s_state.frame_count = 0;
  s_state.frame_limit = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      s_state.frame_limit = atoi(argv[i + 1]);
      i++;
    }
  }

  s_state.canvas =
      bk_gfx_canvas_create(bk_gpu(), &(BK_GfxCanvasDesc){.width = CANVAS_W,
                                                         .height = CANVAS_H,
                                                         .depth_stencil = true,
                                                         .blit_filter = BK_GFX_FILTER_NEAREST});
  if (s_state.canvas == nullptr) {
    return BK_FAIL;
  }

  BK_GfxShaderDesc vertex_shader = s_load_depth_tri_shader("vertex");
  BK_GfxShaderDesc fragment_shader = s_load_depth_tri_shader("fragment");

  BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
  BK_GfxVertexAttribute vertex_attributes[2] = {
      {.location = 0,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT3,
       .offset = offsetof(Vertex, position)},
      {.location = 1,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_UBYTE4_NORM,
       .offset = offsetof(Vertex, color)   },
  };
  BK_GfxPipelineDesc pipeline_desc = {
      .vertex_shader = vertex_shader,
      .fragment_shader = fragment_shader,
      .vertex_buffers = &vertex_buffer_layout,
      .num_vertex_buffers = 1,
      .vertex_attributes = vertex_attributes,
      .num_vertex_attributes = 2,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      // The canvas's own format, not the swapchain's -- bk_gfx_bind_canvas redirects
      // the render pass to the canvas, so the pipeline must match *that* target.
      .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_mode = BK_GFX_BLEND_NONE,
      .depth_stencil_format = bk_gfx_depth_stencil_format(bk_gpu()),
      .depth_compare = BK_GFX_COMPARE_LESS,
      .depth_write = true,
  };
  s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &pipeline_desc);

  s_free_shader(&vertex_shader);
  s_free_shader(&fragment_shader);

  if (s_state.pipeline == nullptr) {
    return BK_FAIL;
  }

  // Two large triangles mirrored top/bottom so they overlap in a hexagram-shaped
  // region in the middle -- red is nearer (z=0.25), blue is farther (z=0.75).
  // Whichever triangle is nearer must show through in the overlap, regardless of
  // draw order (the vertex buffer below draws blue first, red second, on purpose:
  // if depth testing weren't wired up, the overlap would show blue instead).
  Vertex vertices[6] = {
      // far (blue)
      {.position = {-0.8f, 0.8f, 0.75f},  .color = {0, 0, 255, 255}},
      {.position = {0.8f, 0.8f, 0.75f},   .color = {0, 0, 255, 255}},
      {.position = {0.0f, -0.8f, 0.75f},  .color = {0, 0, 255, 255}},
      // near (red)
      {.position = {-0.8f, -0.8f, 0.25f}, .color = {255, 0, 0, 255}},
      {.position = {0.8f, -0.8f, 0.25f},  .color = {255, 0, 0, 255}},
      {.position = {0.0f, 0.8f, 0.25f},   .color = {255, 0, 0, 255}},
  };
  s_state.vertex_buffer =
      bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
  if (s_state.vertex_buffer == nullptr ||
      !bk_gfx_buffer_upload(s_state.vertex_buffer, vertices, 0, sizeof vertices)) {
    return BK_FAIL;
  }

  *state = &s_state;
  return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as 01_clear/02_ticks/03_triangle.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
  (void)frame;
  AppState *app = state;
  app->frame_count++;
  if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

// render: bind the canvas, pipeline, and vertex buffer, then draw both triangles as
// one 6-vertex call. The framework's frame pipeline renders into the canvas, blits it
// onto the swapchain, and presents right after render returns.
static void app_render(void *state, const BK_FrameInfo *frame) {
  (void)frame;
  AppState *app = state;
  bk_gfx_bind_canvas(app->canvas);
  bk_gfx_bind_pipeline(app->pipeline);
  bk_gfx_bind_vertex_buffer(app->vertex_buffer);
  bk_gfx_draw(6);
}

static BK_Result app_event(void *state, const SDL_Event *event) {
  (void)state;
  if (event->type == SDL_EVENT_QUIT) {
    return BK_DONE;
  }
  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
    return BK_DONE;
  }
  if (event->type == SDL_EVENT_WINDOW_RESIZED) {
    // bk_window_size is already current here -- the framework refreshes it
    // before any app .event handler runs. The canvas stays pinned at
    // CANVAS_W x CANVAS_H regardless; only the blit's destination changes.
    i32 width = 0, height = 0;
    bk_window_size(&width, &height);
    SDL_Log("BK: window resized to %dx%d", width, height);
  }
  return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
  (void)result;
  AppState *app = state;
  bk_gfx_buffer_destroy(app->vertex_buffer);
  bk_gfx_pipeline_destroy(app->pipeline);
  bk_gfx_canvas_destroy(app->canvas);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .window = {.title = "06_canvas", .width = 960, .height = 540, .resizable = true},
      .init = app_init,
      .update = app_update,
      .render = app_render,
      .event = app_event,
      .quit = app_quit,
  };
  return bk_run(&desc, argc, argv);
}
#else
BK_APP(.window = {.title = "06_canvas", .width = 960, .height = 540, .resizable = true},
       .init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
