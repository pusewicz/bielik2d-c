#include <bielik/bk_time.h>

void bk_clock_init(BK_Clock *clock, i32 tick_hz, i32 max_ticks_per_frame, f64 max_frame_dt,
                   u64 now_ns) {
    if (max_frame_dt <= 0.0) {
        // Casting a negative double to uint64_t is undefined behavior, and
        // 0.0 itself would clamp every frame's dt to zero forever (raw_ns <
        // 0 is never true for an unsigned raw_ns) — both are nonsensical
        // for a hitch clamp, so treat them as "unset" and fall back to the
        // same 0.25s default BK_TimeDesc documents and bk__boot substitutes
        // for the exactly-zero case.
        max_frame_dt = 0.25;
    }
    if (max_ticks_per_frame < 1) {
        // A negative cap wraps to ~1.8e19 when cast to uint64_t below,
        // silently disabling the spiral-of-death cap entirely.
        max_ticks_per_frame = 1;
    }

    clock->fixed_dt_ns = tick_hz > 0 ? 1000000000ULL / (u64)tick_hz : 0;
    clock->max_frame_ns = (u64)(max_frame_dt * 1e9);
    clock->max_ticks_per_frame = max_ticks_per_frame;
    clock->accumulator_ns = 0;
    clock->tick = 0;
    clock->started = false;
    clock->last_now_ns = now_ns;
}

BK_ClockFrame bk_clock_advance(BK_Clock *clock, u64 now_ns) {
    u64 frame_dt_ns;

    if (!clock->started) {
        clock->started = true;
        clock->last_now_ns = now_ns;
        frame_dt_ns = 0;
    } else {
        u64 raw_ns = now_ns < clock->last_now_ns ? 0 : now_ns - clock->last_now_ns;
        frame_dt_ns = raw_ns < clock->max_frame_ns ? raw_ns : clock->max_frame_ns;
        clock->last_now_ns = now_ns;
    }

    if (clock->fixed_dt_ns == 0) {
        clock->tick += 1;
        return (BK_ClockFrame){
            .ticks = 1,
            .frame_dt = (f64)frame_dt_ns / 1e9,
            .alpha = 1.0,
        };
    }

    clock->accumulator_ns += frame_dt_ns;
    u64 uncapped_ticks = clock->accumulator_ns / clock->fixed_dt_ns;
    u64 ticks = uncapped_ticks < (u64)clock->max_ticks_per_frame
                         ? uncapped_ticks
                         : (u64)clock->max_ticks_per_frame;
    clock->accumulator_ns -= ticks * clock->fixed_dt_ns;
    if (uncapped_ticks > (u64)clock->max_ticks_per_frame) {
        clock->accumulator_ns %= clock->fixed_dt_ns;
    }
    clock->tick += ticks;

    return (BK_ClockFrame){
        .ticks = (i32)ticks,
        .frame_dt = (f64)frame_dt_ns / 1e9,
        .alpha = (f64)clock->accumulator_ns / (f64)clock->fixed_dt_ns,
    };
}

f64 bk_clock_fixed_dt(const BK_Clock *clock) {
    return clock->fixed_dt_ns == 0 ? 0.0 : (f64)clock->fixed_dt_ns / 1e9;
}

f64 bk_clock_sim_time(const BK_Clock *clock) { return (f64)clock->tick * bk_clock_fixed_dt(clock); }
