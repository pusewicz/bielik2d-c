#include "bk_test.h"

#include <bielik/bk_time.h>

static void test_fixed_60hz_steady_steps(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, 0.25, 0);

  const u64 frame_ns = 16666667ULL;
  const int frames = 100000;
  u64 now_ns = 0;
  u64 total_fed_ns = 0;

  for (int i = 0; i < frames; i++) {
    now_ns += frame_ns;
    BK_ClockFrame frame = bk_clock_advance(&clock, now_ns);
    REQUIRE(frame.alpha >= 0.0 && frame.alpha < 1.0);
    if (i > 0) {
      total_fed_ns += frame_ns;
    }
  }

  const u64 fixed_dt_ns = 1000000000ULL / 60ULL;
  const u64 expected_ticks = total_fed_ns / fixed_dt_ns;
  const u64 diff =
      clock.tick > expected_ticks ? clock.tick - expected_ticks : expected_ticks - clock.tick;
  REQUIRE(diff <= 1);

  const f64 sim_time = bk_clock_sim_time(&clock);
  const f64 wall_s = (f64)total_fed_ns / 1e9;
  REQUIRE_NEAR(sim_time, wall_s, 1.0 / 60.0);
}

static void test_hitch_clamp(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, 0.25, 0);
  bk_clock_advance(&clock, 0);

  u64 now_ns = 2000000000ULL;
  BK_ClockFrame frame = bk_clock_advance(&clock, now_ns);

  const int uncapped_hitch_ticks = (int)(0.25 / (1.0 / 60.0));
  const int expected_hitch_ticks = uncapped_hitch_ticks < 8 ? uncapped_hitch_ticks : 8;
  REQUIRE(frame.ticks == expected_hitch_ticks);
  REQUIRE(frame.alpha >= 0.0 && frame.alpha < 1.0);

  now_ns += 1000000000ULL / 60ULL;
  BK_ClockFrame frame2 = bk_clock_advance(&clock, now_ns);
  REQUIRE(frame2.ticks == 1);
  REQUIRE(frame2.alpha >= 0.0 && frame2.alpha < 1.0);
}

static void test_spiral_cap_sustained_load(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, 0.25, 0);
  bk_clock_advance(&clock, 0);

  u64 now_ns = 0;

  const u64 below_cap_frame_ns = 100000000ULL;
  for (int i = 0; i < 10; i++) {
    now_ns += below_cap_frame_ns;
    BK_ClockFrame frame = bk_clock_advance(&clock, now_ns);
    REQUIRE(frame.ticks == 6);
    REQUIRE(frame.alpha >= 0.0 && frame.alpha < 1.0);
  }

  const u64 above_cap_frame_ns = 200000000ULL;
  for (int i = 0; i < 10; i++) {
    now_ns += above_cap_frame_ns;
    BK_ClockFrame frame = bk_clock_advance(&clock, now_ns);
    REQUIRE(frame.ticks == 8);
    REQUIRE(frame.alpha >= 0.0 && frame.alpha < 1.0);
  }
}

static void test_variable_mode(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 0, 8, 0.25, 0);
  bk_clock_advance(&clock, 0);

  const u64 deltas_ns[] = {10000000ULL, 33000000ULL, 8000000ULL, 50000000ULL, 400000000ULL};
  const usize count = sizeof(deltas_ns) / sizeof(deltas_ns[0]);
  const f64 max_frame_dt = 0.25;

  u64 now_ns = 0;
  for (usize i = 0; i < count; i++) {
    now_ns += deltas_ns[i];
    BK_ClockFrame frame = bk_clock_advance(&clock, now_ns);
    REQUIRE(frame.ticks == 1);
    const f64 input_s = (f64)deltas_ns[i] / 1e9;
    const f64 expected_dt = input_s < max_frame_dt ? input_s : max_frame_dt;
    REQUIRE_NEAR(frame.frame_dt, expected_dt, 1e-9);
    REQUIRE(frame.alpha == 1.0);
  }

  REQUIRE(bk_clock_fixed_dt(&clock) == 0.0);
  REQUIRE(bk_clock_sim_time(&clock) == 0.0);
}

static void test_first_frame_dt_zero(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, 0.25, 1000000000000ULL);
  BK_ClockFrame frame = bk_clock_advance(&clock, 5000000000000ULL);
  REQUIRE(frame.frame_dt == 0.0);
  REQUIRE(frame.ticks == 0);
  REQUIRE(frame.alpha == 0.0);

  BK_Clock vclock;
  bk_clock_init(&vclock, 0, 8, 0.25, 1000000000000ULL);
  BK_ClockFrame vframe = bk_clock_advance(&vclock, 5000000000000ULL);
  REQUIRE(vframe.frame_dt == 0.0);
  REQUIRE(vframe.ticks == 1);
  REQUIRE(vframe.alpha == 1.0);
}

static void test_non_monotonic_input(void) {
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, 0.25, 0);
  bk_clock_advance(&clock, 0);

  const u64 forward_ns = 5000000ULL;
  BK_ClockFrame frame1 = bk_clock_advance(&clock, forward_ns);
  REQUIRE(frame1.ticks == 0);

  const u64 backlog_before = clock.accumulator_ns;
  const u64 backward_ns = forward_ns - 1000000ULL;
  BK_ClockFrame frame2 = bk_clock_advance(&clock, backward_ns);
  REQUIRE(frame2.ticks == 0);
  REQUIRE_EQ_U64(clock.accumulator_ns, backlog_before);
  const f64 fixed_dt_ns_d = (f64)(1000000000ULL / 60ULL);
  REQUIRE_NEAR(frame2.alpha, (f64)backlog_before / fixed_dt_ns_d, 1e-12);

  BK_Clock vclock;
  bk_clock_init(&vclock, 0, 8, 0.25, 0);
  bk_clock_advance(&vclock, 0);
  bk_clock_advance(&vclock, 20000000ULL);
  BK_ClockFrame vframe = bk_clock_advance(&vclock, 10000000ULL);
  REQUIRE(vframe.ticks == 1);
  REQUIRE(vframe.frame_dt == 0.0);
  REQUIRE(vframe.alpha == 1.0);
}

static void test_negative_timedesc_fields_clamped(void) {
  // max_frame_dt < 0: without clamping, (uint64_t)(negative * 1e9) is UB
  // and in practice yields max_frame_ns == 0, which then clamps every
  // frame's dt to zero forever (raw_ns < 0 is never true for an unsigned
  // raw_ns). Confirm a normal input delta still produces a sane, non-zero
  // frame_dt instead of getting stuck at zero.
  BK_Clock clock;
  bk_clock_init(&clock, 60, 8, -1.0, 0);
  bk_clock_advance(&clock, 0);

  const u64 normal_frame_ns = 16666667ULL;
  BK_ClockFrame frame = bk_clock_advance(&clock, normal_frame_ns);
  REQUIRE(frame.frame_dt > 0.0);
  REQUIRE_NEAR(frame.frame_dt, (f64)normal_frame_ns / 1e9, 1e-9);

  // max_ticks_per_frame < 0: without clamping, (uint64_t)max_ticks_per_frame
  // wraps to ~1.8e19, so the spiral-of-death cap check (uncapped_ticks >
  // cap) is never true and the cap silently stops capping. Confirm a huge
  // synthetic gap still caps at a small positive tick count, not thousands.
  BK_Clock clock2;
  bk_clock_init(&clock2, 60, -5, 0.25, 0);
  bk_clock_advance(&clock2, 0);

  BK_ClockFrame frame2 = bk_clock_advance(&clock2, 10000000000ULL); // 10 real seconds
  REQUIRE(frame2.ticks == 1); // max_ticks_per_frame clamps up to 1
  REQUIRE(frame2.alpha >= 0.0 && frame2.alpha < 1.0);
}

int main(void) {
  test_fixed_60hz_steady_steps();
  test_hitch_clamp();
  test_spiral_cap_sustained_load();
  test_variable_mode();
  test_first_frame_dt_zero();
  test_non_monotonic_input();
  test_negative_timedesc_fields_clamped();
  printf("test_time: OK\n");
  return 0;
}
