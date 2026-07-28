#include <bielik/bk_time.h>

void bk_clock_init(BK_Clock *c, int tick_hz, int max_ticks_per_frame, double max_frame_dt,
                   uint64_t now_ns) {
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

    c->fixed_dt_ns = tick_hz > 0 ? 1000000000ULL / (uint64_t)tick_hz : 0;
    c->max_frame_ns = (uint64_t)(max_frame_dt * 1e9);
    c->max_ticks_per_frame = max_ticks_per_frame;
    c->accumulator_ns = 0;
    c->tick = 0;
    c->started = false;
    c->last_now_ns = now_ns;
}

BK_ClockFrame bk_clock_advance(BK_Clock *c, uint64_t now_ns) {
    uint64_t frame_dt_ns;

    if (!c->started) {
        c->started = true;
        c->last_now_ns = now_ns;
        frame_dt_ns = 0;
    } else {
        uint64_t raw_ns = now_ns < c->last_now_ns ? 0 : now_ns - c->last_now_ns;
        frame_dt_ns = raw_ns < c->max_frame_ns ? raw_ns : c->max_frame_ns;
        c->last_now_ns = now_ns;
    }

    if (c->fixed_dt_ns == 0) {
        c->tick += 1;
        return (BK_ClockFrame){
            .ticks = 1,
            .frame_dt = (double)frame_dt_ns / 1e9,
            .alpha = 1.0,
        };
    }

    c->accumulator_ns += frame_dt_ns;
    uint64_t uncapped_ticks = c->accumulator_ns / c->fixed_dt_ns;
    uint64_t ticks = uncapped_ticks < (uint64_t)c->max_ticks_per_frame
                         ? uncapped_ticks
                         : (uint64_t)c->max_ticks_per_frame;
    c->accumulator_ns -= ticks * c->fixed_dt_ns;
    if (uncapped_ticks > (uint64_t)c->max_ticks_per_frame) {
        c->accumulator_ns %= c->fixed_dt_ns;
    }
    c->tick += ticks;

    return (BK_ClockFrame){
        .ticks = (int)ticks,
        .frame_dt = (double)frame_dt_ns / 1e9,
        .alpha = (double)c->accumulator_ns / (double)c->fixed_dt_ns,
    };
}

double bk_clock_fixed_dt(const BK_Clock *c) {
    return c->fixed_dt_ns == 0 ? 0.0 : (double)c->fixed_dt_ns / 1e9;
}

double bk_clock_sim_time(const BK_Clock *c) { return (double)c->tick * bk_clock_fixed_dt(c); }
