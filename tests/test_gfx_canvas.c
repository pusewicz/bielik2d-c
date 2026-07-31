#include "bk_test.h"
#include "internal/bk_gfx_canvas_internal.h"
#include "internal/bk_gfx_texture_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_canvas.h>
#include <bielik/bk_gfx_texture.h>
#include <stdio.h>

static SDL_GPUDevice *s_create_device(void) {
    // SDL_CreateGPUDevice requires the video subsystem initialized even though no
    // window is ever created here (SDL_GPUSelectBackend calls SDL_GetVideoDevice()
    // internally and errors "Video subsystem not initialized" otherwise).
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false,
        nullptr);
    REQUIRE(device != nullptr);
    return device;
}

static void test_create_canvas_without_depth_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxCanvas *canvas =
        bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 64, .height = 32});
    REQUIRE(canvas != nullptr);

    int w = 0, h = 0;
    bk_gfx_canvas_size(canvas, &w, &h);
    REQUIRE_EQ_U64((uint64_t)w, 64);
    REQUIRE_EQ_U64((uint64_t)h, 32);

    BK_GfxTexture *color = bk_gfx_canvas_texture(canvas);
    REQUIRE(color != nullptr);
    REQUIRE(bk__gfx_texture_format(color) == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

    REQUIRE(bk__gfx_canvas_depth_handle(canvas) == nullptr);
    REQUIRE(bk__gfx_canvas_depth_format(canvas) == SDL_GPU_TEXTUREFORMAT_INVALID);

    bk_gfx_canvas_destroy(canvas);
    SDL_DestroyGPUDevice(device);
}

static void test_create_canvas_with_depth_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxCanvas *canvas = bk_gfx_canvas_create(
        device, &(BK_GfxCanvasDesc){.width = 16, .height = 16, .depth_stencil = true});
    REQUIRE(canvas != nullptr);

    REQUIRE(bk__gfx_canvas_depth_handle(canvas) != nullptr);
    REQUIRE(bk__gfx_canvas_depth_format(canvas) == bk_gfx_depth_stencil_format(device));

    bk_gfx_canvas_destroy(canvas);
    SDL_DestroyGPUDevice(device);
}

static void test_create_canvas_with_bad_dimensions_returns_null(void) {
    SDL_GPUDevice *device = s_create_device();

    REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 0, .height = 16}) == nullptr);
    REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = 16, .height = 0}) == nullptr);
    REQUIRE(bk_gfx_canvas_create(device, &(BK_GfxCanvasDesc){.width = -1, .height = 16}) ==
            nullptr);

    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_canvas_destroy(nullptr); }

int main(void) {
    test_create_canvas_without_depth_succeeds();
    test_create_canvas_with_depth_succeeds();
    test_create_canvas_with_bad_dimensions_returns_null();
    test_destroy_null_is_noop();
    printf("test_gfx_canvas: OK\n");
    return 0;
}
