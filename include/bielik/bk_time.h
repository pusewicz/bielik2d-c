#pragma once
#include <stdbool.h>
#include <stdint.h>

/// Fixed/variable timestep accumulator state; owns no resources.
typedef struct BK_Clock {
    uint64_t fixed_dt_ns;    // 0 => variable mode
    uint64_t max_frame_ns;   // hitch clamp
    int max_ticks_per_frame; // spiral cap
    uint64_t accumulator_ns;
    uint64_t last_now_ns;
    uint64_t tick; // total fixed ticks since init
    bool started;
} BK_Clock;

/// Result of one bk_clock_advance call: ticks to run this frame and the
/// render-interpolation alpha.
typedef struct BK_ClockFrame {
    int ticks;       // fixed updates to run this frame (always 1 in variable mode)
    double frame_dt; // clamped wall delta, seconds
    double alpha;    // accumulator / fixed_dt in [0,1); 1.0 in variable mode
} BK_ClockFrame;

/// Initializes a clock for either fixed-tick (tick_hz > 0) or variable-dt (tick_hz == 0) mode.
/// max_frame_dt <= 0.0 is treated as unset and substituted with 0.25; max_ticks_per_frame < 1
/// is substituted with 1 — both would otherwise be nonsensical (a frozen clock or a silently
/// disabled spiral-of-death cap).
void bk_clock_init(BK_Clock *c, int tick_hz, int max_ticks_per_frame, double max_frame_dt,
                   uint64_t now_ns);
/// Advances the clock to now_ns, returning the fixed ticks to run and interpolation alpha.
BK_ClockFrame bk_clock_advance(BK_Clock *c, uint64_t now_ns);
/// Returns the fixed timestep in seconds, or 0.0 in variable mode.
double bk_clock_fixed_dt(const BK_Clock *c); // seconds; 0.0 in variable mode
/// Returns tick * fixed_dt, recomputed in double each call (never accumulated).
double bk_clock_sim_time(const BK_Clock *c); // tick * fixed_dt, in double
