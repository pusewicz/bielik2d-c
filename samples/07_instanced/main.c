// 07_instanced -- one draw call, many quads. Per-instance position, size and color
// live in a storage buffer the vertex shader indexes with gl_InstanceIndex; the camera
// arrives as a pushed uniform. This is the shape Phase 3's sprite batch is built on:
// the batcher uploads one buffer of per-sprite data and issues a single instanced draw
// rather than a draw per sprite.
//
// It is also the first sample to use bk_math: bk_m3x2_ortho builds the projection, and
// bk_m3x2_mul composes the camera into it.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_main.h>
#include <bielik/bk_math.h>

#include <stddef.h>
#include <stdlib.h>

constexpr int GRID_COLUMNS = 8;
constexpr int GRID_ROWS = 6;
constexpr int QUAD_COUNT = GRID_COLUMNS * GRID_ROWS;

// The world is this many units wide/tall; the ortho projection maps it onto NDC, so
// the quad positions below are in world units rather than clip space. (04_textured_quad
// had to hardcode clip-space coordinates -- this is what bk_math buys.)
constexpr f32 WORLD_WIDTH = 800.0f;
constexpr f32 WORLD_HEIGHT = 600.0f;

// Mirrors the `Quad` struct in shaders/instanced.vert. std430 packs a struct of two
// vec2s plus a vec4 with no padding, so the C layout matches directly -- but the
// static_assert below is what actually holds that true if a field is ever added.
typedef struct Quad {
  f32 center[2];
  f32 half_size[2];
  f32 color[4];
} Quad;

static_assert(sizeof(Quad) == 32, "Quad must match the shader's std430 layout");

// The vertex uniform block: a BK_M3x2 as two vec4s. std140 requires vec4 alignment, so
// the transform's six floats travel as eight.
typedef struct CameraUniform {
  f32 basis[4];  // x.x, x.y, y.x, y.y
  f32 origin[4]; // origin.x, origin.y, padding, padding
} CameraUniform;

static_assert(sizeof(CameraUniform) == 32, "CameraUniform must match std140 layout");

typedef struct AppState {
  BK_GfxPipeline *pipeline;
  BK_GfxBuffer *quads;
  int frame_count;
  int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state;

static void *s_load_shader_file(const char *relative_path, usize *out_size) {
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

static BK_GfxShaderDesc s_load_instanced_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "instanced.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "instanced.%s.msl", stage);

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

static BK_Result app_init(void **state, int argc, char **argv) {
  *state = &s_state;

  for (int i = 1; i < argc; i++) {
    if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      s_state.frame_limit = SDL_atoi(argv[i + 1]);
    }
  }

  BK_GfxShaderDesc vertex = s_load_instanced_shader("vertex");
  BK_GfxShaderDesc fragment = s_load_instanced_shader("fragment");
  if (vertex.spirv.code == nullptr || fragment.spirv.code == nullptr) {
    return BK_FAIL;
  }
  // These counts must match what the shader binary declares. A mismatch does not fail
  // here or at pipeline creation -- it silently drops the command buffer at draw time,
  // and the window reads back as all zeros. Check them first on a blank frame.
  vertex.num_storage_buffers = 1;
  vertex.num_uniform_buffers = 1;

  BK_GfxPipelineDesc desc = {
      .vertex_shader = vertex,
      .fragment_shader = fragment,
      // No vertex buffer at all: the shader builds each quad's corners from
      // gl_VertexIndex and reads its placement from the storage buffer.
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_STRIP,
      .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
      .blend_mode = BK_GFX_BLEND_PREMULTIPLIED,
  };
  s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);
  s_free_shader(&vertex);
  s_free_shader(&fragment);
  if (s_state.pipeline == nullptr) {
    return BK_FAIL;
  }

  // Lay the quads out in a grid, tinted by position so instancing is obvious at a
  // glance: if every quad landed in the same place, or all shared one color, the
  // vertex shader would be ignoring gl_InstanceIndex.
  Quad quads[QUAD_COUNT];
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int column = 0; column < GRID_COLUMNS; column++) {
      int index = row * GRID_COLUMNS + column;
      f32 tx = (f32)column / (f32)(GRID_COLUMNS - 1);
      f32 ty = (f32)row / (f32)(GRID_ROWS - 1);
      quads[index] = (Quad){
          .center = {bk_lerpf(-320.0f, 320.0f, tx), bk_lerpf(-220.0f, 220.0f, ty)},
          .half_size = {28.0f, 28.0f},
          .color = {tx, 0.35f, 1.0f - tx, 1.0f},
      };
    }
  }

  s_state.quads =
      bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_GRAPHICS, sizeof quads);
  if (s_state.quads == nullptr) {
    return BK_FAIL;
  }
  if (!bk_gfx_buffer_upload(s_state.quads, quads, 0, sizeof quads)) {
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

static void app_render(void *state, const BK_FrameInfo *frame) {
  AppState *app = state;

  // Slowly rotate and breathe the camera, so the transform is visibly doing work
  // rather than sitting at identity.
  f32 angle = (f32)frame->real_time * 0.25f;
  f32 zoom = 1.0f + 0.15f * SDL_sinf((f32)frame->real_time);
  BK_M3x2 camera = bk_m3x2_trs(bk_v2_zero(), bk_v2(zoom, zoom), angle);
  // The MVP maps world units onto NDC. The camera transform is inverted because moving
  // the camera right must move the world left.
  BK_M3x2 mvp = bk_m3x2_mul(bk_m3x2_ortho(WORLD_WIDTH, WORLD_HEIGHT), bk_m3x2_inv(camera));

  CameraUniform uniform = {
      .basis = {mvp.x.x,      mvp.x.y,      mvp.y.x, mvp.y.y},
      .origin = {mvp.origin.x, mvp.origin.y, 0.0f,    0.0f   },
  };

  bk_gfx_bind_pipeline(app->pipeline);
  bk_gfx_bind_vertex_storage_buffer(app->quads, 0);
  // Safe to pass a stack struct: the push copies into the frame arena, since the GPU
  // push doesn't happen until the frame's flush.
  bk_gfx_push_vertex_uniform(&uniform, sizeof uniform);
  // Four vertices (one triangle strip) per instance, QUAD_COUNT instances, one call.
  bk_gfx_draw_instanced(4, QUAD_COUNT);
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
  bk_gfx_buffer_destroy(app->quads);
  bk_gfx_pipeline_destroy(app->pipeline);
  app->quads = nullptr;
  app->pipeline = nullptr;
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .window = {.title = "07_instanced", .width = 1280, .height = 720},
      .init = app_init,
      .update = app_update,
      .render = app_render,
      .event = app_event,
      .quit = app_quit,
  };
  return bk_run(&desc, argc, argv);
}
#else
BK_APP(.window = {.title = "07_instanced", .width = 1280, .height = 720}, .init = app_init,
       .update = app_update, .render = app_render, .event = app_event, .quit = app_quit, )
#endif
