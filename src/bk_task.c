#include "internal/bk_task_internal.h"
#include <bielik/bk_task.h>
#include <stddef.h>

static BK_TaskSystemDesc s_desc;

void bk__task_set_desc(const BK_TaskSystemDesc *desc) {
    s_desc = desc ? *desc : (BK_TaskSystemDesc){0};
}

void bk_task_run(BK_TaskFn fn, int32_t count, int32_t min_range, void *arg) {
    BK_ASSERT(fn != nullptr);

    if (count <= 0) {
        return;
    }

    if (s_desc.enqueue == nullptr) {
        fn(0, count, 0, arg);
        return;
    }

    void *task = s_desc.enqueue(fn, count, min_range, arg, s_desc.ctx);
    if (s_desc.finish != nullptr) {
        s_desc.finish(task, s_desc.ctx);
    }
}
