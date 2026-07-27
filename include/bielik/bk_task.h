#pragma once
#include <bielik/bk_app.h>

/// Runs fn over [0,count) honoring the app's task system (serial in v1).
/// Blocks until complete. min_range is a splitting hint for real schedulers.
void bk_task_run(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg);
