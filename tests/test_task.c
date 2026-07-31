#include "bk_test.h"
#include "internal/bk_task_internal.h"

#include <bielik/bk_task.h>

typedef struct RangeSpy {
  int buf[64];
  int call_count;
  i32 last_start;
  i32 last_end;
} RangeSpy;

static void range_fn(i32 start, i32 end, u32 worker_index, void *arg) {
  RangeSpy *spy = (RangeSpy *)arg;
  spy->call_count++;
  spy->last_start = start;
  spy->last_end = end;
  REQUIRE(worker_index == 0);
  for (i32 i = start; i < end; i++) {
    spy->buf[i]++;
  }
}

static void test_serial_executor_covers_full_range_once(void) {
  bk__task_set_desc(nullptr);

  RangeSpy spy = {0};
  const i32 count = 32;
  bk_task_run(range_fn, count, 4, &spy);

  REQUIRE(spy.call_count == 1);
  REQUIRE(spy.last_start == 0);
  REQUIRE(spy.last_end == count);
  for (i32 i = 0; i < count; i++) {
    REQUIRE(spy.buf[i] == 1);
  }
  for (i32 i = count; i < 64; i++) {
    REQUIRE(spy.buf[i] == 0);
  }
}

static void counting_fn(i32 start, i32 end, u32 worker_index, void *arg) {
  (void)start;
  (void)end;
  (void)worker_index;
  int *call_count = (int *)arg;
  (*call_count)++;
}

static void test_zero_and_negative_count_call_nothing(void) {
  bk__task_set_desc(nullptr);

  int call_count = 0;
  bk_task_run(counting_fn, 0, 1, &call_count);
  bk_task_run(counting_fn, -5, 1, &call_count);
  REQUIRE(call_count == 0);
}

typedef struct EnqueueCall {
  BK_TaskFn fn;
  i32 count;
  i32 min_range;
  void *arg;
  void *ctx;
  int calls;
} EnqueueCall;

typedef struct FinishCall {
  void *task;
  void *ctx;
  int calls;
} FinishCall;

typedef struct CustomTaskSystemSpy {
  EnqueueCall enqueue;
  FinishCall finish;
} CustomTaskSystemSpy;

static int s_task_token;

static void *fake_enqueue(BK_TaskFn fn, i32 count, i32 min_range, void *arg, void *ctx) {
  CustomTaskSystemSpy *spy = (CustomTaskSystemSpy *)ctx;
  spy->enqueue.fn = fn;
  spy->enqueue.count = count;
  spy->enqueue.min_range = min_range;
  spy->enqueue.arg = arg;
  spy->enqueue.ctx = ctx;
  spy->enqueue.calls++;
  return &s_task_token;
}

static void fake_finish(void *task, void *ctx) {
  CustomTaskSystemSpy *spy = (CustomTaskSystemSpy *)ctx;
  spy->finish.task = task;
  spy->finish.ctx = ctx;
  spy->finish.calls++;
}

static void unused_fn(i32 start, i32 end, u32 worker_index, void *arg) {
  (void)start;
  (void)end;
  (void)worker_index;
  (void)arg;
}

static void test_custom_desc_pass_through(void) {
  CustomTaskSystemSpy spy = {0};
  BK_TaskSystemDesc desc = {
      .ctx = &spy,
      .enqueue = fake_enqueue,
      .finish = fake_finish,
  };
  bk__task_set_desc(&desc);

  int dummy_arg = 0;
  bk_task_run(unused_fn, 10, 3, &dummy_arg);

  REQUIRE(spy.enqueue.calls == 1);
  REQUIRE(spy.enqueue.fn == unused_fn);
  REQUIRE(spy.enqueue.count == 10);
  REQUIRE(spy.enqueue.min_range == 3);
  REQUIRE(spy.enqueue.arg == &dummy_arg);
  REQUIRE(spy.enqueue.ctx == &spy);

  REQUIRE(spy.finish.calls == 1);
  REQUIRE(spy.finish.task == &s_task_token);
  REQUIRE(spy.finish.ctx == &spy);

  bk__task_set_desc(nullptr);
}

static void test_custom_desc_without_finish_and_zero_count(void) {
  CustomTaskSystemSpy spy = {0};
  BK_TaskSystemDesc desc = {
      .ctx = &spy,
      .enqueue = fake_enqueue,
  };
  bk__task_set_desc(&desc);

  bk_task_run(unused_fn, 0, 1, &spy);
  REQUIRE(spy.enqueue.calls == 0);

  bk_task_run(unused_fn, 10, 3, &spy);
  REQUIRE(spy.enqueue.calls == 1);
  REQUIRE(spy.finish.calls == 0);

  bk__task_set_desc(nullptr);
}

int main(void) {
  test_serial_executor_covers_full_range_once();
  test_zero_and_negative_count_call_nothing();
  test_custom_desc_pass_through();
  test_custom_desc_without_finish_and_zero_count();
  printf("test_task: OK\n");
  return 0;
}
