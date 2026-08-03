#pragma once
#include <bielik/bk_types.h>

#include <SDL3/SDL_test_assert.h>

#include <stdlib.h>

// Assertions for the test binaries, layered on SDL_test's SDLTest_AssertCheck so
// failures route through SDL's logging (stderr is invisible on Android, iOS, and the
// Windows GUI subsystem) and feed SDLTest's pass/fail counters, which the summary
// below prints.
//
// Use SDLTest_AssertCheck, never SDLTest_Assert: the latter's body is
// SDL_assert(SDLTest_AssertCheck(...)), so the whole call sits inside SDL_assert's
// argument and is swallowed by its sizeof at assert level <= 1 -- no check, no log,
// no counter, in exactly the Release builds where you would want it.
//
// REQUIRE* stay FATAL even though SDLTest_AssertCheck itself records-and-continues:
// most call sites dereference right after (REQUIRE(device != nullptr), then use
// device), so continuing past a failure would segfault instead of reporting. CHECK is
// the non-fatal variant for assertions that stand alone.

/// Fails the test at the first failed condition, after logging it and the running
/// pass/fail summary. The common case -- use this unless a failure is genuinely safe
/// to continue past.
#define REQUIRE(cond)                                                                            \
  do {                                                                                           \
    if (!SDLTest_AssertCheck((cond) ? 1 : 0, "%s:%d: REQUIRE(%s)", __FILE__, __LINE__, #cond)) { \
      SDLTest_LogAssertSummary();                                                                \
      exit(1);                                                                                   \
    }                                                                                            \
  } while (0)

/// Non-fatal REQUIRE: logs and counts a failure, then keeps going, so one run reports
/// every failure instead of just the first. Only safe where nothing after it depends
/// on the condition holding. Evaluates to the condition, so it can also be branched on.
#define CHECK(cond) \
  SDLTest_AssertCheck((cond) ? 1 : 0, "%s:%d: CHECK(%s)", __FILE__, __LINE__, #cond)

#define REQUIRE_EQ_U64(a, b)                                                                     \
  do {                                                                                           \
    u64 bk_test_a_ = (a);                                                                        \
    u64 bk_test_b_ = (b);                                                                        \
    if (!SDLTest_AssertCheck(                                                                    \
            bk_test_a_ == bk_test_b_, "%s:%d: REQUIRE_EQ_U64(%s, %s): %llu vs %llu", __FILE__,   \
            __LINE__, #a, #b, (unsigned long long)bk_test_a_, (unsigned long long)bk_test_b_)) { \
      SDLTest_LogAssertSummary();                                                                \
      exit(1);                                                                                   \
    }                                                                                            \
  } while (0)

#define REQUIRE_NEAR(a, b, eps)                                                               \
  do {                                                                                        \
    f64 bk_test_a_ = (a);                                                                     \
    f64 bk_test_b_ = (b);                                                                     \
    f64 bk_test_diff_ = bk_test_a_ - bk_test_b_;                                              \
    if (bk_test_diff_ < 0) {                                                                  \
      bk_test_diff_ = -bk_test_diff_;                                                         \
    }                                                                                         \
    if (!SDLTest_AssertCheck(bk_test_diff_ <= (eps), "%s:%d: REQUIRE_NEAR(%s, %s): %f vs %f", \
                             __FILE__, __LINE__, #a, #b, bk_test_a_, bk_test_b_)) {           \
      SDLTest_LogAssertSummary();                                                             \
      exit(1);                                                                                \
    }                                                                                         \
  } while (0)
