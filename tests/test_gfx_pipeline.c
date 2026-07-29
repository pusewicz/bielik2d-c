#include "bk_test.h"
#include "internal/bk_gfx_pipeline_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_pipeline.h>
#include <stdio.h>

static void *s_load_shader_file(const char *relative_path, size_t *out_size) {
    char path[512];
    SDL_snprintf(path, sizeof path, "%sshaders/%s", SDL_GetBasePath(), relative_path);
    void *data = SDL_LoadFile(path, out_size);
    REQUIRE(data != nullptr);
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

static void test_create_and_destroy_pipeline_succeeds(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);

    BK_GfxShaderDesc vertex = s_load_triangle_shader("vertex");
    BK_GfxShaderDesc fragment = s_load_triangle_shader("fragment");

    BK_GfxPipelineDesc desc = {
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .primitive_type = BK_GFX_PRIMITIVE_TRIANGLE_LIST,
        .color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_mode = BK_GFX_BLEND_NONE,
    };

    BK_GfxPipeline *pipeline = bk_gfx_pipeline_create(device, &desc);
    REQUIRE(pipeline != nullptr);

    bk_gfx_pipeline_destroy(pipeline);
    s_free_shader(&vertex);
    s_free_shader(&fragment);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_pipeline_destroy(nullptr); }

int main(void) {
    test_create_and_destroy_pipeline_succeeds();
    test_destroy_null_is_noop();
    printf("test_gfx_pipeline: OK\n");
    return 0;
}
