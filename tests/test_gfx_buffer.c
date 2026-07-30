#include "bk_test.h"
#include "internal/bk_gfx_buffer_internal.h"
#include <SDL3/SDL.h>
#include <bielik/bk_gfx_buffer.h>
#include <stdint.h>
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

static void test_create_each_usage_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *vertex = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 256);
    REQUIRE(vertex != nullptr);
    REQUIRE(bk__gfx_buffer_size(vertex) == 256);

    BK_GfxBuffer *index = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_INDEX, 128);
    REQUIRE(index != nullptr);

    BK_GfxBuffer *storage = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_STORAGE_READ, 64);
    REQUIRE(storage != nullptr);

    bk_gfx_buffer_destroy(vertex);
    bk_gfx_buffer_destroy(index);
    bk_gfx_buffer_destroy(storage);
    SDL_DestroyGPUDevice(device);
}

static void test_upload_in_bounds_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    uint8_t data[32];
    for (int i = 0; i < 32; i++) {
        data[i] = (uint8_t)i;
    }

    REQUIRE(bk_gfx_buffer_upload(buffer, data, 0, sizeof data));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_upload_at_offset_succeeds(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    uint8_t data[16] = {0};
    REQUIRE(bk_gfx_buffer_upload(buffer, data, 32, sizeof data));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_oversized_upload_returns_false(void) {
    SDL_GPUDevice *device = s_create_device();

    BK_GfxBuffer *buffer = bk_gfx_buffer_create(device, BK_GFX_BUFFER_USAGE_VERTEX, 64);
    REQUIRE(buffer != nullptr);

    uint8_t data[8] = {0};

    // size alone exceeds the buffer.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 0, 128));
    // offset + size exceeds the buffer, neither alone would.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 60, 8));
    // offset == buffer size, any positive size is out of bounds.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, 64, 1));
    // offset alone is already out of bounds; offset + size must not wrap uint32_t.
    REQUIRE(!bk_gfx_buffer_upload(buffer, data, UINT32_MAX - 4, 8));

    bk_gfx_buffer_destroy(buffer);
    SDL_DestroyGPUDevice(device);
}

static void test_destroy_null_is_noop(void) { bk_gfx_buffer_destroy(nullptr); }

int main(void) {
    test_create_each_usage_succeeds();
    test_upload_in_bounds_succeeds();
    test_upload_at_offset_succeeds();
    test_oversized_upload_returns_false();
    test_destroy_null_is_noop();
    printf("test_gfx_buffer: OK\n");
    return 0;
}
