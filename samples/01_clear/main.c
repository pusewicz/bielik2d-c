// 01_clear — the smallest possible Bielik2D app.
//
// Demonstrates: the BK_APP entry-point macro, the init/update/render/event
// callback shape, reading command-line args in init, and driving gfx state
// (the clear color) from render. Built twice by CMake: once as `01_clear`
// (the default BK_APP + SDL main-callbacks path) and once as
// `01_clear_run` (the alternate bk_run()-direct path, for tools/tests that
// want to own their own main()) — see the #ifdef at the bottom.

#include <bielik/bk_gfx.h>
#include <bielik/bk_main.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// App state, allocated by init and handed back to every other callback.
// This sample only needs a frame counter and an optional frame limit.
typedef struct AppState {
    int frame_count;
    int frame_limit; // 0 => no limit (the default; run until closed/ESC)
} AppState;

static AppState s_state; // a static works fine for a single-app-at-a-time sample

// init: SDL isn't touched here directly — the framework has already created
// the window and GPU device by the time init runs. This is where you parse
// command-line args and set up your own game state.
static BK_Result app_init(void **state, int argc, char **argv) {
    s_state.frame_count = 0;
    s_state.frame_limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_state.frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }
    *state = &s_state;
    return BK_CONTINUE;
}

// update: runs once per frame in variable-dt mode (tick_hz == 0). Returning
// anything other than BK_CONTINUE ends the app — used here to support
// `--frames N` for CI smoke testing, so the sample terminates on its own
// instead of waiting for a window close.
static BK_Result app_update(void *state, const BK_FrameInfo *f) {
    (void)f;
    AppState *s = state;
    s->frame_count++;
    if (s->frame_limit > 0 && s->frame_count >= s->frame_limit) {
        return BK_DONE;
    }
    return BK_CONTINUE;
}

// render: the whole point of this sample — cycle the swapchain clear color
// through a slow rainbow using sinf(real_time), phase-shifted per channel.
// bk_gfx_set_clear_color just records the color; the framework's frame
// pipeline calls bk_gfx__flush (clear + present) right after render returns.
static void app_render(void *state, const BK_FrameInfo *f) {
    (void)state;
    float t = (float)f->real_time;
    BK_Color color = {
        .r = 0.5f + 0.5f * sinf(t),
        .g = 0.5f + 0.5f * sinf(t + 2.0943951f), // +2*pi/3
        .b = 0.5f + 0.5f * sinf(t + 4.1887902f), // +4*pi/3
        .a = 1.0f,
    };
    bk_gfx_set_clear_color(color);
}

// event: the framework forwards every SDL event here since this app sets
// .event on its BK_AppDesc — with .event set, WE own quit handling
// entirely (the framework's built-in SDL_EVENT_QUIT-only handling only
// kicks in when .event is left NULL).
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

// Two ways to boot this app, picked at compile time:
//
// Default (BK_APP): SDL owns main() via its main-callbacks machinery.
// This is the blessed path for a real game.
//
// BK_MAIN_HANDLED (defined by the `01_clear_run` CMake target below): we
// write our own main() and call bk_run() ourselves — the path meant for
// tools/tests that need to own their own entry point.
#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
    BK_AppDesc desc = {
        .init = app_init,
        .update = app_update,
        .render = app_render,
        .event = app_event,
    };
    return bk_run(&desc, argc, argv);
}
#else
BK_APP(.init = app_init, .update = app_update, .render = app_render, .event = app_event, )
#endif
