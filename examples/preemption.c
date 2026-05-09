/**
 * preemption.c - 抢占与公平调度示例
 *
 * 展示信号抢占（coco_preempt_enable/disable）、
 * 时间片公平调度（coco_sched_set_fairness）、
 * 以及协作式抢占检查点（coco_preempt_checkpoint）。
 */

#include "coco.h"
#include <stdio.h>

static int g_count_a = 0;
static int g_count_b = 0;

/* CPU 密集型协程 A：不主动 yield，依赖抢占 */
void cpu_bound_a(void *arg) {
    (void)arg;
    printf("Coro A: started (CPU-bound, relies on preemption)\n");

    coco_preempt_enable();

    for (int i = 0; i < 500000; i++) {
        g_count_a++;

        /* 偶尔插入协作式检查点，减少对信号抢占的依赖 */
        if (i % 10000 == 0) {
            coco_preempt_checkpoint();
        }
    }

    coco_preempt_disable();
    printf("Coro A: finished, count = %d\n", g_count_a);
}

/* CPU 密集型协程 B：与 A 竞争 CPU */
void cpu_bound_b(void *arg) {
    (void)arg;
    printf("Coro B: started (CPU-bound, relies on preemption)\n");

    coco_preempt_enable();

    for (int i = 0; i < 500000; i++) {
        g_count_b++;

        if (i % 10000 == 0) {
            coco_preempt_checkpoint();
        }
    }

    coco_preempt_disable();
    printf("Coro B: finished, count = %d\n", g_count_b);
}

/* 观察者协程：定期报告进度 */
void observer(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        coco_sleep(20);
        printf("Observer: A=%d, B=%d\n", g_count_a, g_count_b);
    }
}

int main(void) {
    printf("=== Preemption & Fairness Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    /* 启用时间片公平调度，每 10ms 强制切换 */
    coco_sched_set_fairness(sched, true, 10);
    printf("Fairness enabled: time slice = 10ms\n\n");

    coco_create(sched, cpu_bound_a, NULL, COCO_STACK_MEDIUM);
    coco_create(sched, cpu_bound_b, NULL, COCO_STACK_MEDIUM);
    coco_create(sched, observer, NULL, 0);

    coco_sched_run(sched);

    printf("\nFinal counts: A=%d, B=%d\n", g_count_a, g_count_b);
    printf("With fairness, both coroutines get roughly equal CPU time\n");

    coco_sched_destroy(sched);

    printf("\n✅ Preemption & Fairness example completed\n");
    return 0;
}
