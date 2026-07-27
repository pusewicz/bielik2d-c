#include "bk_test.h"
#include <bielik/bk_time.h>
#include <stddef.h>

static void test_fixed_60hz_steady_steps(void) {
    BK_Clock c;
    bk_clock_init(&c, 60, 8, 0.25, 0);

    const uint64_t frame_ns = 16666667ULL;
    const int frames = 100000;
    uint64_t now_ns = 0;
    uint64_t total_fed_ns = 0;

    for (int i = 0; i < frames; i++) {
        now_ns += frame_ns;
        BK_ClockFrame f = bk_clock_advance(&c, now_ns);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
        if (i > 0) {
            total_fed_ns += frame_ns;
        }
    }

    const uint64_t fixed_dt_ns = 1000000000ULL / 60ULL;
    const uint64_t expected_ticks = total_fed_ns / fixed_dt_ns;
    const uint64_t diff =
        c.tick > expected_ticks ? c.tick - expected_ticks : expected_ticks - c.tick;
    REQUIRE(diff <= 1);

    const double sim_time = bk_clock_sim_time(&c);
    const double wall_s = (double)total_fed_ns / 1e9;
    REQUIRE_NEAR(sim_time, wall_s, 1.0 / 60.0);
}

static void test_hitch_clamp(void) {
    BK_Clock c;
    bk_clock_init(&c, 60, 8, 0.25, 0);
    bk_clock_advance(&c, 0);

    uint64_t now_ns = 2000000000ULL;
    BK_ClockFrame f = bk_clock_advance(&c, now_ns);

    const int uncapped_hitch_ticks = (int)(0.25 / (1.0 / 60.0));
    const int expected_hitch_ticks = uncapped_hitch_ticks < 8 ? uncapped_hitch_ticks : 8;
    REQUIRE(f.ticks == expected_hitch_ticks);
    REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);

    now_ns += 1000000000ULL / 60ULL;
    BK_ClockFrame f2 = bk_clock_advance(&c, now_ns);
    REQUIRE(f2.ticks == 1);
    REQUIRE(f2.alpha >= 0.0 && f2.alpha < 1.0);
}

static void test_spiral_cap_sustained_load(void) {
    BK_Clock c;
    bk_clock_init(&c, 60, 8, 0.25, 0);
    bk_clock_advance(&c, 0);

    uint64_t now_ns = 0;

    const uint64_t below_cap_frame_ns = 100000000ULL;
    for (int i = 0; i < 10; i++) {
        now_ns += below_cap_frame_ns;
        BK_ClockFrame f = bk_clock_advance(&c, now_ns);
        REQUIRE(f.ticks == 6);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
    }

    const uint64_t above_cap_frame_ns = 200000000ULL;
    for (int i = 0; i < 10; i++) {
        now_ns += above_cap_frame_ns;
        BK_ClockFrame f = bk_clock_advance(&c, now_ns);
        REQUIRE(f.ticks == 8);
        REQUIRE(f.alpha >= 0.0 && f.alpha < 1.0);
    }
}

static void test_variable_mode(void) {
    BK_Clock c;
    bk_clock_init(&c, 0, 8, 0.25, 0);
    bk_clock_advance(&c, 0);

    const uint64_t deltas_ns[] = {10000000ULL, 33000000ULL, 8000000ULL, 50000000ULL, 400000000ULL};
    const size_t n = sizeof(deltas_ns) / sizeof(deltas_ns[0]);
    const double max_frame_dt = 0.25;

    uint64_t now_ns = 0;
    for (size_t i = 0; i < n; i++) {
        now_ns += deltas_ns[i];
        BK_ClockFrame f = bk_clock_advance(&c, now_ns);
        REQUIRE(f.ticks == 1);
        const double input_s = (double)deltas_ns[i] / 1e9;
        const double expected_dt = input_s < max_frame_dt ? input_s : max_frame_dt;
        REQUIRE_NEAR(f.frame_dt, expected_dt, 1e-9);
        REQUIRE(f.alpha == 1.0);
    }

    REQUIRE(bk_clock_fixed_dt(&c) == 0.0);
    REQUIRE(bk_clock_sim_time(&c) == 0.0);
}

static void test_first_frame_dt_zero(void) {
    BK_Clock c;
    bk_clock_init(&c, 60, 8, 0.25, 1000000000000ULL);
    BK_ClockFrame f = bk_clock_advance(&c, 5000000000000ULL);
    REQUIRE(f.frame_dt == 0.0);
    REQUIRE(f.ticks == 0);
    REQUIRE(f.alpha == 0.0);

    BK_Clock vc;
    bk_clock_init(&vc, 0, 8, 0.25, 1000000000000ULL);
    BK_ClockFrame vf = bk_clock_advance(&vc, 5000000000000ULL);
    REQUIRE(vf.frame_dt == 0.0);
    REQUIRE(vf.ticks == 1);
    REQUIRE(vf.alpha == 1.0);
}

static void test_non_monotonic_input(void) {
    BK_Clock c;
    bk_clock_init(&c, 60, 8, 0.25, 0);
    bk_clock_advance(&c, 0);

    const uint64_t forward_ns = 5000000ULL;
    BK_ClockFrame f1 = bk_clock_advance(&c, forward_ns);
    REQUIRE(f1.ticks == 0);

    const uint64_t backlog_before = c.accumulator_ns;
    const uint64_t backward_ns = forward_ns - 1000000ULL;
    BK_ClockFrame f2 = bk_clock_advance(&c, backward_ns);
    REQUIRE(f2.ticks == 0);
    REQUIRE_EQ_U64(c.accumulator_ns, backlog_before);
    const double fixed_dt_ns_d = (double)(1000000000ULL / 60ULL);
    REQUIRE_NEAR(f2.alpha, (double)backlog_before / fixed_dt_ns_d, 1e-12);

    BK_Clock vc;
    bk_clock_init(&vc, 0, 8, 0.25, 0);
    bk_clock_advance(&vc, 0);
    bk_clock_advance(&vc, 20000000ULL);
    BK_ClockFrame vf = bk_clock_advance(&vc, 10000000ULL);
    REQUIRE(vf.ticks == 1);
    REQUIRE(vf.frame_dt == 0.0);
    REQUIRE(vf.alpha == 1.0);
}

int main(void) {
    test_fixed_60hz_steady_steps();
    test_hitch_clamp();
    test_spiral_cap_sustained_load();
    test_variable_mode();
    test_first_frame_dt_zero();
    test_non_monotonic_input();
    printf("test_time: OK\n");
    return 0;
}
