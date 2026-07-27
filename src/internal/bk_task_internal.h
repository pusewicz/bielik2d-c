#pragma once
#include <bielik/bk_app.h>

// Installs the active task system. Passing NULL (or a desc with all fields
// zero) resets to the built-in serial executor. Not part of the public API;
// bk_app.c's boot sequence calls this in a later task. Exposed here so
// tests can exercise custom-desc pass-through without a running app.
void bk__task_set_desc(const BK_TaskSystemDesc *desc);
