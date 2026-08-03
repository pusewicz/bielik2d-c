#pragma once
#include <bielik/bk_types.h>

#include <SDL3/SDL_test_assert.h>
#include <SDL3/SDL_test_compare.h>

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

/// Checks one RGBA8 pixel in a byte buffer -- either a tightly-packed GPU download
/// buffer or a locked SDL_Surface's `pixels` -- against expected channel values within
/// `tolerance` each. `stride_bytes` is the byte length of one row: pass `width * 4` for
/// a packed download buffer, or `surface->pitch` for an SDL_Surface (which may pad
/// rows). Replaces five near-identical per-file `s_check_pixel` copies. Most call sites
/// should prefer REQUIRE_SURFACE for whole-image coverage; this stays useful for a
/// single spot-check (e.g. an alpha channel REQUIRE_SURFACE ignores) or where no
/// CPU-constructible reference image exists (SDF antialiasing in test_draw_gpu.c).
/// Fatal like REQUIRE.
#define REQUIRE_PIXEL(pixels, stride_bytes, x, y, r, g, b, a, tolerance)      \
  do {                                                                        \
    const u8 *bk_test_px_ = (const u8 *)(pixels);                             \
    usize bk_test_i_ = (usize)(y) * (usize)(stride_bytes) + (usize)(x) * 4;   \
    REQUIRE(abs((int)bk_test_px_[bk_test_i_ + 0] - (int)(r)) <= (tolerance)); \
    REQUIRE(abs((int)bk_test_px_[bk_test_i_ + 1] - (int)(g)) <= (tolerance)); \
    REQUIRE(abs((int)bk_test_px_[bk_test_i_ + 2] - (int)(b)) <= (tolerance)); \
    REQUIRE(abs((int)bk_test_px_[bk_test_i_ + 3] - (int)(a)) <= (tolerance)); \
  } while (0)

/// Compares `surface` against `reference` pixel-for-pixel via SDLTest_CompareSurfaces
/// and fails (fatal, like REQUIRE) unless the result is 0..max_failing_pixels
/// mismatching pixels inclusive.
///
/// The two knobs absorb two different kinds of disagreement, and conflating them
/// defeats the point of a whole-image compare:
///   - `allowable_error` (sum of squared per-pixel RGB differences) absorbs
///     quantization/rounding noise that is uniform across the image -- every pixel is
///     subject to it, so it must stay tight. A uniform off-by-one write can't be
///     absorbed by `max_failing_pixels` at all: it would fail on every pixel at once.
///   - `max_failing_pixels` absorbs spatially localized disagreement -- the 1px-wide
///     band along a rasterized/antialiased edge where Metal, Vulkan, and D3D12
///     legitimately pick different pixels. Derive it from the shape's actual edge
///     length (see call sites for worked examples), not as a blanket fudge factor: a
///     budget close to the shape's interior area would let the shape vanish or move
///     and still pass, which defeats the point of converting from probes.
///
/// On failure, reports which of SDLTest_CompareSurfaces's three failure modes hit: a
/// NULL surface (-1), a size mismatch (-2, both sizes reported), or too many differing
/// pixels (count vs. budget). SDLTest_CompareSurfaces itself already logs a detailed
/// sample (first differing pixel, both colors) via SDLTest_LogError.
#define REQUIRE_SURFACE(surface, reference, allowable_error, max_failing_pixels)                 \
  do {                                                                                           \
    SDL_Surface *bk_test_surf_ = (surface);                                                      \
    SDL_Surface *bk_test_ref_ = (reference);                                                     \
    int bk_test_cmp_ = SDLTest_CompareSurfaces(bk_test_surf_, bk_test_ref_, (allowable_error));  \
    bool bk_test_pass_;                                                                          \
    if (bk_test_cmp_ == -1) {                                                                    \
      bk_test_pass_ = SDLTest_AssertCheck(                                                       \
          0, "%s:%d: REQUIRE_SURFACE(%s, %s): a surface was NULL (%s=%p, %s=%p)", __FILE__,      \
          __LINE__, #surface, #reference, #surface, (void *)bk_test_surf_, #reference,           \
          (void *)bk_test_ref_);                                                                 \
    } else if (bk_test_cmp_ == -2) {                                                             \
      bk_test_pass_ = SDLTest_AssertCheck(                                                       \
          0, "%s:%d: REQUIRE_SURFACE(%s, %s): size mismatch, %dx%d vs expected %dx%d", __FILE__, \
          __LINE__, #surface, #reference, bk_test_surf_->w, bk_test_surf_->h, bk_test_ref_->w,   \
          bk_test_ref_->h);                                                                      \
    } else {                                                                                     \
      bk_test_pass_ = SDLTest_AssertCheck(                                                       \
          bk_test_cmp_ <= (max_failing_pixels),                                                  \
          "%s:%d: REQUIRE_SURFACE(%s, %s): %d pixels exceeded allowable_error=%d, budget "       \
          "was %d",                                                                              \
          __FILE__, __LINE__, #surface, #reference, bk_test_cmp_, (allowable_error),             \
          (max_failing_pixels));                                                                 \
    }                                                                                            \
    if (!bk_test_pass_) {                                                                        \
      SDLTest_LogAssertSummary();                                                                \
      exit(1);                                                                                   \
    }                                                                                            \
  } while (0)
