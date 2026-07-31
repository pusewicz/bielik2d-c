// 02_ticks — fixed 60Hz timestep, live tick/render stats, and a deliberate
// hitch demo.
//
// Unlike 01_clear (variable-dt: one update per frame, dt = whatever the
// frame took), this sample runs a FIXED 60Hz simulation step. The frame
// pipeline can run update() zero, one, or several times in a single frame
// depending on how far real time has drifted from sim time (see bk_time.h)
// — that's why renders/sec and ticks/sec are tracked separately below.
//
// Press SPACE to force a 300ms hitch (see update()) and watch the printed
// stats: the frame right after the hitch runs the maximum 8 fixed ticks in
// one go (max_ticks_per_frame — the spiral-of-death cap engaging), and
// whatever backlog is left beyond that cap gets discarded outright rather
// than chased down over several more frames (bk_clock's cap-hit branch
// resets the accumulator to its sub-tick remainder, it doesn't carry the
// rest forward). So the one-second stats window containing the hitch shows
// ticks/sec *dip* (fewer ticks landed in that window, not more) and
// `drift` take a one-time permanent step further negative — not a gradual
// multi-frame climb. Either way, ticks/sec is back to a steady ~60 the very
// next second: bounded and stable, never exploding.

#include <bielik/bk_gfx.h>
#include <bielik/bk_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// App state — a file static works fine for a single-app-at-a-time sample; a
// real game with more complex lifetime needs might allocate this instead.
//
// Tick/render counters are split because they're driven by different
// callbacks that don't run 1:1 in fixed-timestep mode: update() runs once
// per fixed 60Hz tick (0, 1, or several times per frame), render() runs
// exactly once per frame. Both counters are reset every time the once-a-
// second stats line is printed.
typedef struct AppState {
  int tick_count;           // ticks since boot; used for --frames N termination
  int frame_limit;          // 0 => no limit (the default; run until closed/ESC)
  int ticks_this_second;    // ticks since the last stats line
  int renders_this_second;  // frames rendered since the last stats line
  f64 last_print_real_time; // frame->real_time as of the last stats line
  bool hitch_requested;     // set by event() on SPACE, consumed by update()
} AppState;

static AppState s_state;

// init: SDL isn't touched here directly — the framework has already created
// the window and GPU device by the time init runs. This is where you parse
// command-line args and set up your own game state.
static BK_Result app_init(void **state, int argc, char **argv) {
  s_state = (AppState){0};
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      s_state.frame_limit = atoi(argv[i + 1]);
      i++;
    }
  }
  bk_gfx_set_clear_color((BK_Color){0.05f, 0.05f, 0.08f, 1.0f});
  *state = &s_state;
  return BK_CONTINUE;
}

// update: runs once per fixed 60Hz tick — possibly several times in a single
// frame if the app fell behind real time (up to the max_ticks_per_frame
// cap set below), possibly zero times in a frame that arrives faster than
// the tick rate. `--frames N` is interpreted as "run for N fixed ticks"
// here (not N rendered frames) since fixed ticks are the deterministic
// unit in this mode — checking the limit here is exactly where 01_clear's
// equivalent variable-dt check lives, just against the tick counter
// instead of the frame counter.
static BK_Result app_update(void *state, const BK_FrameInfo *frame) {
  (void)frame;
  AppState *app = state;
  app->tick_count++;
  app->ticks_this_second++;

  if (app->hitch_requested) {
    app->hitch_requested = false;
    // Deliberately naughty: never block inside update() in real game
    // code — a real game would stall input, physics, and audio right
    // along with this thread. Doing it here, once, on purpose, is the
    // whole point of this sample: it manufactures the kind of stall
    // bk_clock's hitch clamp (max_frame_dt) and spiral-of-death cap
    // (max_ticks_per_frame) exist to survive. The very next frame after
    // this delay runs the maximum 8 ticks at once (the cap engaging)
    // and permanently drops whatever backlog is left beyond that —
    // so the next printed stats line shows a *dip* in ticks/sec for
    // that one-second window (not a climb toward hundreds of ticks),
    // and settles back to ~60 right after.
    SDL_Delay(300);
  }

  if (app->frame_limit > 0 && app->tick_count >= app->frame_limit) {
    return BK_DONE;
  }
  return BK_CONTINUE;
}

// render: runs once per frame (regardless of how many ticks ran this
// frame, including zero). This is where render-rate tracking and the
// once-a-second stats line live, since render always has exactly one call
// per frame and gets the interpolation alpha / real_time / sim_time fields
// the tick loop doesn't.
static void app_render(void *state, const BK_FrameInfo *frame) {
  AppState *app = state;
  app->renders_this_second++;

  if (frame->real_time - app->last_print_real_time >= 1.0) {
    // drift = how far the fixed-tick simulation clock has diverged
    // from the wall clock. It should stay small and bounded (well
    // under a second) even across the hitch demo above — that's the
    // clamp/cap combo doing its job instead of letting sim_time run
    // away trying to fully "catch up" in one shot.
    f64 drift = frame->sim_time - frame->real_time;
    printf("renders/sec=%d ticks/sec=%d alpha=%.3f drift=%.4fs\n", app->renders_this_second,
           app->ticks_this_second, frame->alpha, drift);
    app->renders_this_second = 0;
    app->ticks_this_second = 0;
    app->last_print_real_time = frame->real_time;
  }
}

// event: the framework forwards every SDL event here since this app sets
// .event on its BK_AppDesc — with .event set, WE own quit handling
// entirely (the framework's built-in SDL_EVENT_QUIT-only handling only
// kicks in when .event is left NULL). SPACE just sets a flag; the actual
// (deliberately bad) blocking happens in update(), never here.
static BK_Result app_event(void *state, const SDL_Event *event) {
  AppState *app = state;
  if (event->type == SDL_EVENT_QUIT) {
    return BK_DONE;
  }
  if (event->type == SDL_EVENT_KEY_DOWN) {
    if (event->key.key == SDLK_ESCAPE) {
      return BK_DONE;
    }
    if (event->key.key == SDLK_SPACE) {
      app->hitch_requested = true;
    }
  }
  return BK_CONTINUE;
}

// Two ways to boot this app, picked at compile time — see 01_clear's main.c
// for the full explanation. Note the explicit tick_hz/max_ticks_per_frame
// below: 8 is also the framework's own default (see bk_app.c), but this
// sample sets it explicitly since the spiral-of-death cap is the whole
// point of the demo, not an incidental default.
#ifdef BK_MAIN_HANDLED
int main(int argc, char **argv) {
  BK_AppDesc desc = {
      .time = {.tick_hz = 60, .max_ticks_per_frame = 8},
      .init = app_init,
      .update = app_update,
      .render = app_render,
      .event = app_event,
  };
  return bk_run(&desc, argc, argv);
}
#else
BK_APP(.time = {.tick_hz = 60, .max_ticks_per_frame = 8}, .init = app_init, .update = app_update,
       .render = app_render, .event = app_event, )
#endif
