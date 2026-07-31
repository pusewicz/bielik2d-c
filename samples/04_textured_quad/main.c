// 04_textured_quad -- the smallest use of bk_gfx_buffer + bk_gfx_texture together: an
// uploaded vertex buffer (position/uv/color) and index buffer describe a quad, a
// procedurally generated checkerboard texture is sampled with NEAREST filtering. Built
// once, using the BK_APP entry-point macro like 01_clear/03_triangle.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_buffer.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_gfx_texture.h>
#include <bielik/bk_main.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex {
  f32 position[2];
  f32 uv[2];
  u8 color[4];
} Vertex;

constexpr int CHECKERBOARD_SIZE = 8;
constexpr int CHECKERBOARD_TILE = 1; // texels per checker square

typedef struct AppState {
  BK_GfxPipeline *pipeline;
  BK_GfxBuffer *vertex_buffer;
  BK_GfxBuffer *index_buffer;
  BK_GfxTexture *texture;
  BK_GfxSampler *sampler;
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

static BK_GfxShaderDesc s_load_textured_shader(const char *stage) {
  char spv_name[64];
  char msl_name[64];
  SDL_snprintf(spv_name, sizeof spv_name, "textured.%s.spv", stage);
  SDL_snprintf(msl_name, sizeof msl_name, "textured.%s.msl", stage);

  BK_GfxShaderDesc desc = {0};
  desc.spirv.code = s_load_shader_file(spv_name, &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file(msl_name, &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  // textured.frag declares one sampler2D; textured.vert declares none.
  if (SDL_strcmp(stage, "fragment") == 0) {
    desc.num_samplers = 1;
  }
  return desc;
}

static void s_free_shader(BK_GfxShaderDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

// init: build the pipeline, upload the quad's vertex/index data, and generate +
// upload a checkerboard texture, all up front -- the framework has already created
// the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
  s_state.frame_count = 0;
  s_state.frame_limit = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      s_state.frame_limit = atoi(argv[i + 1]);
      i++;
    }
  }

  BK_GfxShaderDesc vertex_shader = s_load_textured_shader("vertex");
  BK_GfxShaderDesc fragment_shader = s_load_textured_shader("fragment");

  BK_GfxVertexBufferLayout vertex_buffer_layout = {.slot = 0, .pitch = sizeof(Vertex)};
  BK_GfxVertexAttribute vertex_attributes[3] = {
      {.location = 0,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
       .offset = offsetof(Vertex, position)},
      {.location = 1,
       .buffer_slot = 0,
       .format = BK_GFX_VERTEX_FORMAT_FLOAT2,
       .offset = offsetof(Vertex, uv)      },
      {.location = 2,
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
      .num_vertex_attributes = 3,
      .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
      .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
      .blend_mode = BK_GFX_BLEND_NONE,
  };
  s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &pipeline_desc);

  s_free_shader(&vertex_shader);
  s_free_shader(&fragment_shader);

  if (s_state.pipeline == nullptr) {
    return BK_FAIL;
  }

  // A half-size quad centered on screen. NDC (-1,-1) is the lower-left corner
  // (SDL_GPU's documented coordinate system), which maps to uv (0,1) -- the
  // texture's bottom-left texel row -- so screen and texture agree on which edge is
  // "down".
  Vertex vertices[4] = {
      {.position = {-0.5f, -0.5f}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
      {.position = {0.5f, -0.5f},  .uv = {1, 1}, .color = {255, 255, 255, 255}},
      {.position = {-0.5f, 0.5f},  .uv = {0, 0}, .color = {255, 255, 255, 255}},
      {.position = {0.5f, 0.5f},   .uv = {1, 0}, .color = {255, 255, 255, 255}},
  };
  s_state.vertex_buffer =
      bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_VERTEX, sizeof vertices);
  if (s_state.vertex_buffer == nullptr ||
      !bk_gfx_buffer_upload(s_state.vertex_buffer, vertices, 0, sizeof vertices)) {
    return BK_FAIL;
  }

  u16 indices[6] = {0, 1, 2, 2, 1, 3};
  s_state.index_buffer = bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_INDEX, sizeof indices);
  if (s_state.index_buffer == nullptr ||
      !bk_gfx_buffer_upload(s_state.index_buffer, indices, 0, sizeof indices)) {
    return BK_FAIL;
  }

  // Procedurally generate a checkerboard: alternating black/white squares.
  u8 checkerboard[CHECKERBOARD_SIZE][CHECKERBOARD_SIZE][4];
  for (int y = 0; y < CHECKERBOARD_SIZE; y++) {
    for (int x = 0; x < CHECKERBOARD_SIZE; x++) {
      bool light = ((x / CHECKERBOARD_TILE) + (y / CHECKERBOARD_TILE)) % 2 == 0;
      u8 value = light ? 255 : 32;
      checkerboard[y][x][0] = value;
      checkerboard[y][x][1] = value;
      checkerboard[y][x][2] = value;
      checkerboard[y][x][3] = 255;
    }
  }
  s_state.texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_SAMPLER, CHECKERBOARD_SIZE,
                                          CHECKERBOARD_SIZE);
  if (s_state.texture == nullptr || !bk_gfx_texture_upload(s_state.texture, checkerboard)) {
    return BK_FAIL;
  }

  s_state.sampler = bk_gfx_sampler_create(bk_gpu(), BK_GFX_FILTER_NEAREST, BK_GFX_ADDRESS_CLAMP);
  if (s_state.sampler == nullptr) {
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

// render: bind everything and draw 6 indices (two triangles forming the quad). The
// framework's frame pipeline calls bk__gfx_flush (clear + bind/draw + present) right
// after render returns.
static void app_render(void *state, const BK_FrameInfo *frame) {
  (void)frame;
  AppState *app = state;
  bk_gfx_bind_pipeline(app->pipeline);
  bk_gfx_bind_vertex_buffer(app->vertex_buffer);
  bk_gfx_bind_index_buffer(app->index_buffer);
  bk_gfx_bind_texture(app->texture, app->sampler);
  bk_gfx_draw_indexed(6);
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
  (void)result;
  AppState *app = state;
  bk_gfx_sampler_destroy(app->sampler);
  bk_gfx_texture_destroy(app->texture);
  bk_gfx_buffer_destroy(app->index_buffer);
  bk_gfx_buffer_destroy(app->vertex_buffer);
  bk_gfx_pipeline_destroy(app->pipeline);
}

#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .init = app_init,
      .update = app_update,
      .render = app_render,
      .event = app_event,
      .quit = app_quit,
  };
  return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event,
       .quit = app_quit, )
#endif
