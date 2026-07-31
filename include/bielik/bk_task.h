#pragma once
#include <bielik/bk_app.h>
#include <bielik/bk_types.h>

/// Runs fn over [0,count) honoring the app's task system (serial in v1).
/// Blocks until complete. min_range is a splitting hint for real schedulers.
void bk_task_run(BK_TaskFn fn, i32 count, i32 min_range, void *arg);
