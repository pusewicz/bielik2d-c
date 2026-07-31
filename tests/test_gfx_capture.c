#include "bk_test.h"
#include <SDL3/SDL.h>
#include <bielik/bk_app.h>
#include <bielik/bk_gfx.h>
#include <stdio.h>
#include <stdlib.h>

static int s_update_calls = 0;
static bool s_capture_requested = false;
static char s_capture_path[512];

static BK_Result test_init(void **state, int argc, char **argv) {
    (void)state;
    (void)argc;
    (void)argv;
    REQUIRE(bk_window() != nullptr);
    REQUIRE(bk_gpu() != nullptr);

    const char *base_path = SDL_GetBasePath();
    REQUIRE(base_path != nullptr);
    SDL_snprintf(s_capture_path, sizeof s_capture_path, "%stest_gfx_capture_output.bmp", base_path);
    SDL_RemovePath(s_capture_path); // in case a prior failed run left one behind
    return BK_CONTINUE;
}

static BK_Result test_update(void *state, const BK_FrameInfo *f) {
    (void)state;
    (void)f;
    s_update_calls++;
    if (s_update_calls >= 3) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

static void test_render(void *state, const BK_FrameInfo *f) {
    (void)state;
    (void)f;
    bk_gfx_set_clear_color((BK_Color){.r = 0.2f, .g = 0.4f, .b = 0.6f, .a = 1.0f});
    // Request on the first render call reached, whichever tick that lands on --
    // robust to fixed-tick batching (multiple ticks can run before one render call).
    if (!s_capture_requested) {
        bk_gfx_request_capture(s_capture_path);
        s_capture_requested = true;
    }
}

int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .window = {.title = "test_gfx_capture", .w = 64, .h = 64},
        .time = {.tick_hz = 60},
        .init = test_init,
        .update = test_update,
        .render = test_render,
    };
    int result = bk_run(&desc, argc, argv);
    REQUIRE(result == 0);
    REQUIRE(s_capture_requested);

    SDL_Surface *loaded = SDL_LoadBMP(s_capture_path);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->w == 64);
    REQUIRE(loaded->h == 64);

    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(rgba != nullptr);
    SDL_DestroySurface(loaded);

    const uint8_t *pixels = (const uint8_t *)rgba->pixels;
    constexpr int tolerance = 5;
    size_t center = ((size_t)(rgba->h / 2) * (size_t)rgba->pitch) + (size_t)(rgba->w / 2) * 4;
    REQUIRE(abs((int)pixels[center + 0] - 51) <= tolerance);  // R = 0.2 * 255
    REQUIRE(abs((int)pixels[center + 1] - 102) <= tolerance); // G = 0.4 * 255
    REQUIRE(abs((int)pixels[center + 2] - 153) <= tolerance); // B = 0.6 * 255
    REQUIRE(abs((int)pixels[center + 3] - 255) <= tolerance); // A

    SDL_DestroySurface(rgba);
    SDL_RemovePath(s_capture_path);
    printf("test_gfx_capture: OK\n");
    return 0;
}
