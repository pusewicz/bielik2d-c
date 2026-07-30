// 03_triangle — the smallest possible use of bk_gfx_pipeline: load the precompiled
// shader bytecode produced offline (see shaders/triangle.{vert,frag} and
// cmake/shaders.cmake), build a pipeline, and draw a hardcoded triangle with no
// vertex buffer (the vertex shader generates positions from gl_VertexIndex). Built
// once, using the BK_APP entry-point macro like 01_clear.

#include <bielik/bk_gfx.h>
#include <bielik/bk_gfx_pipeline.h>
#include <bielik/bk_main.h>

#include <stdlib.h>
#include <string.h>

typedef struct AppState {
    BK_GfxPipeline *pipeline;
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

static BK_GfxShaderDesc s_load_triangle_shader(const char *stage) {
    char spv_name[64];
    char msl_name[64];
    SDL_snprintf(spv_name, sizeof spv_name, "triangle.%s.spv", stage);
    SDL_snprintf(msl_name, sizeof msl_name, "triangle.%s.msl", stage);

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

// init: create the pipeline here, once, up front -- the framework has already
// created the window and GPU device by the time init runs.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GetGPUSwapchainTextureFormat(bk_gpu(), bk_window()),
        .blend_mode = BK_GFX_BLEND_NONE,
    };
    s_state.pipeline = bk_gfx_pipeline_create(bk_gpu(), &desc);

    s_free_shader(&vertex);
    s_free_shader(&fragment);

    if (s_state.pipeline == nullptr) {
        return BK_FAIL;
    }

    *state = &s_state;
    return BK_CONTINUE;
}

// update: supports --frames N for CI smoke testing, same as 01_clear/02_ticks.
static BK_Result app_update(void *state, const BK_FrameInfo *f) {
    (void)f;
    AppState *s = state;
    s->frame_count++;
    if (s->frame_limit > 0 && s->frame_count >= s->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: bind the pipeline and draw 3 vertices. The framework's frame pipeline
// calls bk__gfx_flush (clear + bind/draw + present) right after render returns.
static void app_render(void *state, const BK_FrameInfo *f) {
    (void)f;
    AppState *s = state;
    bk_gfx_bind_pipeline(s->pipeline);
    bk_gfx_draw(3);
}

static BK_Result app_event(void *state, const SDL_Event *e) {
    (void)state;
    if (e->type == SDL_EVENT_QUIT) {
        return BK_DONE;
    }
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void app_quit(void *state, BK_Result result) {
    (void)result;
    AppState *s = state;
    bk_gfx_pipeline_destroy(s->pipeline);
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
