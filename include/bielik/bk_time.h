#pragma once
#include <bielik/bk_types.h>

#include <stdbool.h>

/// Fixed/variable timestep accumulator state; owns no resources.
typedef struct BK_Clock {
  u64 fixed_dt_ns;         // 0 => variable mode
  u64 max_frame_ns;        // hitch clamp
  i32 max_ticks_per_frame; // spiral cap
  u64 accumulator_ns;
  u64 last_now_ns;
  u64 tick; // total fixed ticks since init
  bool started;
} BK_Clock;

/// Result of one bk_clock_advance call: ticks to run this frame and the
/// render-interpolation alpha.
typedef struct BK_ClockFrame {
  i32 ticks;    // fixed updates to run this frame (always 1 in variable mode)
  f64 frame_dt; // clamped wall delta, seconds
  f64 alpha;    // accumulator / fixed_dt in [0,1); 1.0 in variable mode
} BK_ClockFrame;

/// Initializes a clock for either fixed-tick (tick_hz > 0) or variable-dt (tick_hz == 0) mode.
/// max_frame_dt <= 0.0 is treated as unset and substituted with 0.25; max_ticks_per_frame < 1
/// is substituted with 1 — both would otherwise be nonsensical (a frozen clock or a silently
/// disabled spiral-of-death cap).
void bk_clock_init(BK_Clock *clock, i32 tick_hz, i32 max_ticks_per_frame, f64 max_frame_dt,
                   u64 now_ns);
/// Advances the clock to now_ns, returning the fixed ticks to run and interpolation alpha.
BK_ClockFrame bk_clock_advance(BK_Clock *clock, u64 now_ns);
/// Returns the fixed timestep in seconds, or 0.0 in variable mode.
f64 bk_clock_fixed_dt(const BK_Clock *clock); // seconds; 0.0 in variable mode
/// Returns tick * fixed_dt, recomputed in double each call (never accumulated).
f64 bk_clock_sim_time(const BK_Clock *clock); // tick * fixed_dt, in double
