#pragma once
#include <stdint.h>

#define BK_VERSION_MAJOR 0
#define BK_VERSION_MINOR 1
#define BK_VERSION_PATCH 0

/// Returns the library version as "MAJOR.MINOR.PATCH".
const char *bk_version_string(void);

// Runs fn over a sub-range of [0,count) on one worker. Shape matches Box2D
// v3's task callback so a real scheduler can plug in without adaptation.
typedef void (*BK_TaskFn)(int32_t start, int32_t end, uint32_t worker_index, void *arg);

// Describes a pluggable task system. All-zero (every field NULL) means: use
// the built-in single-threaded serial executor.
typedef struct BK_TaskSystemDesc {
    void *ctx;
    void *(*enqueue)(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg, void *ctx);
    void (*finish)(void *task, void *ctx);
} BK_TaskSystemDesc;
