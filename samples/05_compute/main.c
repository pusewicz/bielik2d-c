// 05_compute -- the smallest use of bk_gfx_pipeline's compute support: a compute
// shader (gradient.comp) fills a texture once at init time, reading its parameters
// from an uploaded storage buffer, and the same textured-quad pipeline as
// 04_textured_quad draws it every frame. Built once, using the BK_APP entry-point
// macro like 01_clear/03_triangle/04_textured_quad.

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

constexpr int GRADIENT_SIZE = 256;

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

static BK_GfxComputePipelineDesc s_load_gradient_compute_desc(void) {
  BK_GfxComputePipelineDesc desc = {
      .num_readonly_storage_buffers = 1,
      .num_readwrite_storage_textures = 1,
      .threadcount_x = 8,
      .threadcount_y = 8,
      .threadcount_z = 1,
  };
  desc.spirv.code = s_load_shader_file("gradient.compute.spv", &desc.spirv.code_size);
  desc.spirv.entry_point = "main";
  desc.msl.code = s_load_shader_file("gradient.compute.msl", &desc.msl.code_size);
  desc.msl.entry_point = "main0";
  return desc;
}

static void s_free_compute_desc(BK_GfxComputePipelineDesc *desc) {
  SDL_free((void *)desc->spirv.code);
  SDL_free((void *)desc->msl.code);
}

// Fills texture once via a compute dispatch: reads {base_color, scale} from a
// throwaway storage buffer and writes a gradient. bk_gfx_compute_dispatch is
// synchronous (see bk_gfx_pipeline.h), so this is deliberately called once here in
// init, not from render every frame.
static bool s_fill_gradient(BK_GfxTexture *texture) {
  BK_GfxComputePipelineDesc compute_desc = s_load_gradient_compute_desc();
  BK_GfxComputePipeline *compute_pipeline = bk_gfx_compute_pipeline_create(bk_gpu(), &compute_desc);
  s_free_compute_desc(&compute_desc);
  if (compute_pipeline == nullptr) {
    return false;
  }

  f32 params[8] = {0.1f, 0.2f, 0.6f, 1.0f, 0.8f, 0.6f, 0.0f, 0.0f};
  BK_GfxBuffer *params_buffer =
      bk_gfx_buffer_create(bk_gpu(), BK_GFX_BUFFER_USAGE_STORAGE_READ, sizeof params);
  if (params_buffer == nullptr) {
    bk_gfx_compute_pipeline_destroy(compute_pipeline);
    return false;
  }
  if (!bk_gfx_buffer_upload(params_buffer, params, 0, sizeof params)) {
    bk_gfx_buffer_destroy(params_buffer);
    bk_gfx_compute_pipeline_destroy(compute_pipeline);
    return false;
  }

  BK_GfxBuffer *readonly_buffers[1] = {params_buffer};
  BK_GfxTexture *readwrite_textures[1] = {texture};
  BK_GfxComputeDispatchDesc dispatch_desc = {
      .pipeline = compute_pipeline,
      .readwrite_textures = readwrite_textures,
      .num_readwrite_textures = 1,
      .readonly_buffers = readonly_buffers,
      .num_readonly_buffers = 1,
      .groups_x = (GRADIENT_SIZE + 7) / 8,
      .groups_y = (GRADIENT_SIZE + 7) / 8,
      .groups_z = 1,
  };
  bool ok = bk_gfx_compute_dispatch(&dispatch_desc);

  bk_gfx_buffer_destroy(params_buffer);
  bk_gfx_compute_pipeline_destroy(compute_pipeline);
  return ok;
}

// init: build the graphics pipeline, upload the quad's vertex/index data, create the
// compute-target texture and fill it via a one-time dispatch -- the framework has
// already created the window and GPU device by the time init runs.
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

  // A full-viewport quad. NDC (-1,-1) is the lower-left corner (SDL_GPU's
  // documented coordinate system), which maps to uv (0,1) -- the texture's
  // bottom-left texel row -- so screen and texture agree on which edge is "down".
  Vertex vertices[4] = {
      {.position = {-1, -1}, .uv = {0, 1}, .color = {255, 255, 255, 255}},
      {.position = {1, -1},  .uv = {1, 1}, .color = {255, 255, 255, 255}},
      {.position = {-1, 1},  .uv = {0, 0}, .color = {255, 255, 255, 255}},
      {.position = {1, 1},   .uv = {1, 0}, .color = {255, 255, 255, 255}},
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

  s_state.texture = bk_gfx_texture_create(bk_gpu(), BK_GFX_TEXTURE_USAGE_COMPUTE_TARGET,
                                          GRADIENT_SIZE, GRADIENT_SIZE);
  if (s_state.texture == nullptr || !s_fill_gradient(s_state.texture)) {
    return BK_FAIL;
  }

  // LINEAR, unlike 04_textured_quad's NEAREST checkerboard -- the right choice for
  // a smooth gradient.
  s_state.sampler = bk_gfx_sampler_create(bk_gpu(), BK_GFX_FILTER_LINEAR, BK_GFX_ADDRESS_CLAMP);
  if (s_state.sampler == nullptr) {
    return BK_FAIL;
  }

  *state = &s_state;
  return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as the other samples.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
  (void)frame;
  AppState *app = state;
  app->frame_count++;
  if (app->frame_limit > 0 && app->frame_count >= app->frame_limit) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

// render: bind everything and draw the same 6 indices every frame -- the gradient
// texture was filled once in init, not re-dispatched here.
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
