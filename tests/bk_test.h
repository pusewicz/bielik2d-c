#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, \
                     #cond);                                                 \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define REQUIRE_EQ_U64(a, b)                                                 \
    do {                                                                     \
        uint64_t bk_test_a_ = (a);                                           \
        uint64_t bk_test_b_ = (b);                                           \
        if (bk_test_a_ != bk_test_b_) {                                      \
            fprintf(stderr,                                                  \
                     "%s:%d: REQUIRE_EQ_U64 failed: %s (%llu) != %s (%llu)\n", \
                     __FILE__, __LINE__, #a,                                 \
                     (unsigned long long)bk_test_a_, #b,                     \
                     (unsigned long long)bk_test_b_);                        \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define REQUIRE_NEAR(a, b, eps)                                              \
    do {                                                                     \
        double bk_test_a_ = (a);                                             \
        double bk_test_b_ = (b);                                            \
        double bk_test_diff_ = bk_test_a_ - bk_test_b_;                      \
        if (bk_test_diff_ < 0) {                                             \
            bk_test_diff_ = -bk_test_diff_;                                  \
        }                                                                    \
        if (bk_test_diff_ > (eps)) {                                         \
            fprintf(stderr,                                                  \
                     "%s:%d: REQUIRE_NEAR failed: %s (%f) != %s (%f)\n",     \
                     __FILE__, __LINE__, #a, bk_test_a_, #b, bk_test_b_);     \
            exit(1);                                                         \
        }                                                                    \
    } while (0)
