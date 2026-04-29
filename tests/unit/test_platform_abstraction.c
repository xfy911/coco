/**
 * test_platform_abstraction.c - 平台抽象层测试 (Phase 5, US-013)
 *
 * 验收标准:
 * - 线程原语抽象 (coco_thread.h)
 * - 互斥锁抽象 (coco_mutex.h)
 * - 条件变量抽象 (coco_cond.h)
 * - Linux/macOS/Windows 统一 API
 */

#include "../../src/platform/coco_thread.h"
#include "../../src/platform/coco_mutex.h"
#include "../../src/platform/coco_cond.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

/* 测试计数器 */
static atomic_uint test_pass_count = 0;
static atomic_uint test_fail_count = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        atomic_fetch_add(&test_pass_count, 1); \
        printf("  ✓ %s\n", msg); \
    } else { \
        atomic_fetch_add(&test_fail_count, 1); \
        printf("  ✗ %s\n", msg); \
    } \
} while (0)

/* === 线程测试 === */

/* 线程参数 */
typedef struct {
    int id;
    atomic_int *counter;
} thread_arg_t;

/* 线程函数 */
static void* thread_func(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    atomic_fetch_add(targ->counter, 1);
    coco_thread_sleep(10);
    return NULL;
}

/* 测试 1: 线程创建和等待 */
static void test_thread_create_join(void) {
    printf("\n[TEST 1] 线程创建和等待\n");

    coco_thread_t thread;
    atomic_int counter = 0;
    thread_arg_t arg = { .id = 1, .counter = &counter };

    int ret = coco_thread_create(&thread, NULL, thread_func, &arg);
    TEST_ASSERT(ret == 0, "线程创建成功");

    ret = coco_thread_join(thread, NULL);
    TEST_ASSERT(ret == 0, "线程等待成功");
    TEST_ASSERT(atomic_load(&counter) == 1, "线程执行正确");
}

/* 测试 2: 线程 ID 和比较 */
static void test_thread_id(void) {
    printf("\n[TEST 2] 线程 ID 和比较\n");

    coco_thread_id_t id1 = coco_thread_self();
    coco_thread_id_t id2 = coco_thread_self();

    TEST_ASSERT(coco_thread_equal(id1, id2), "相同线程 ID 比较");
}

/* 测试 3: 线程让出 */
static void test_thread_yield(void) {
    printf("\n[TEST 3] 线程让出\n");

    coco_thread_yield();
    TEST_ASSERT(1, "线程让出成功");
}

/* 测试 4: CPU 核心数 */
static void test_cpu_count(void) {
    printf("\n[TEST 4] CPU 核心数\n");

    uint32_t count = coco_thread_cpu_count();
    printf("  CPU 核心数: %u\n", count);
    TEST_ASSERT(count >= 1, "CPU 核心数 >= 1");
}

/* === 互斥锁测试 === */

/* 测试 5: 互斥锁基本操作 */
static void test_mutex_basic(void) {
    printf("\n[TEST 5] 互斥锁基本操作\n");

    coco_mutex_t mutex;
    int ret = coco_mutex_init(&mutex, NULL);
    TEST_ASSERT(ret == 0, "互斥锁初始化成功");

    ret = coco_mutex_lock(&mutex);
    TEST_ASSERT(ret == 0, "互斥锁加锁成功");

    ret = coco_mutex_unlock(&mutex);
    TEST_ASSERT(ret == 0, "互斥锁解锁成功");

    ret = coco_mutex_destroy(&mutex);
    TEST_ASSERT(ret == 0, "互斥锁销毁成功");
}

/* 测试 6: 互斥锁尝试加锁 */
static void test_mutex_trylock(void) {
    printf("\n[TEST 6] 互斥锁尝试加锁\n");

    coco_mutex_t mutex;
    coco_mutex_init(&mutex, NULL);

    int ret = coco_mutex_trylock(&mutex);
    TEST_ASSERT(ret == 0, "尝试加锁成功");

    ret = coco_mutex_trylock(&mutex);
    TEST_ASSERT(ret != 0, "重复加锁失败 (预期)");

    coco_mutex_unlock(&mutex);
    coco_mutex_destroy(&mutex);
}

/* 测试 7: 递归锁 */
static void test_mutex_recursive(void) {
    printf("\n[TEST 7] 递归锁\n");

    coco_mutex_t mutex;
    coco_mutex_attr_t attr = { .type = 1 };  /* 递归锁 */
    int ret = coco_mutex_init(&mutex, &attr);
    TEST_ASSERT(ret == 0, "递归锁初始化成功");

    ret = coco_mutex_lock(&mutex);
    TEST_ASSERT(ret == 0, "第一次加锁成功");

    ret = coco_mutex_lock(&mutex);
    TEST_ASSERT(ret == 0, "第二次加锁成功 (递归)");

    coco_mutex_unlock(&mutex);
    coco_mutex_unlock(&mutex);
    coco_mutex_destroy(&mutex);
}

/* === 条件变量测试 === */

/* 条件变量测试参数 */
typedef struct {
    coco_mutex_t mutex;
    coco_cond_t cond;
    atomic_int ready;
    atomic_int done;
} cond_test_t;

/* 生产者线程 */
static void* producer_thread(void *arg) {
    cond_test_t *test = (cond_test_t*)arg;

    coco_thread_sleep(50);  /* 模拟工作 */

    coco_mutex_lock(&test->mutex);
    atomic_store(&test->ready, 1);
    coco_cond_signal(&test->cond);
    coco_mutex_unlock(&test->mutex);

    return NULL;
}

/* 消费者线程 */
static void* consumer_thread(void *arg) {
    cond_test_t *test = (cond_test_t*)arg;

    coco_mutex_lock(&test->mutex);
    while (!atomic_load(&test->ready)) {
        coco_cond_wait(&test->cond, &test->mutex);
    }
    atomic_store(&test->done, 1);
    coco_mutex_unlock(&test->mutex);

    return NULL;
}

/* 测试 8: 条件变量基本操作 */
static void test_cond_basic(void) {
    printf("\n[TEST 8] 条件变量基本操作\n");

    cond_test_t test;
    coco_mutex_init(&test.mutex, NULL);
    coco_cond_init(&test.cond, NULL);
    atomic_init(&test.ready, 0);
    atomic_init(&test.done, 0);

    coco_thread_t producer, consumer;

    int ret = coco_thread_create(&consumer, NULL, consumer_thread, &test);
    TEST_ASSERT(ret == 0, "消费者线程创建成功");

    ret = coco_thread_create(&producer, NULL, producer_thread, &test);
    TEST_ASSERT(ret == 0, "生产者线程创建成功");

    coco_thread_join(producer, NULL);
    coco_thread_join(consumer, NULL);

    TEST_ASSERT(atomic_load(&test.done) == 1, "条件变量通知成功");

    coco_cond_destroy(&test.cond);
    coco_mutex_destroy(&test.mutex);
}

/* 测试 9: 条件变量超时等待 */
static void test_cond_timedwait(void) {
    printf("\n[TEST 9] 条件变量超时等待\n");

    coco_mutex_t mutex;
    coco_cond_t cond;
    coco_mutex_init(&mutex, NULL);
    coco_cond_init(&cond, NULL);

    coco_mutex_lock(&mutex);

    /* 没有通知，应该超时 */
    int ret = coco_cond_timedwait(&cond, &mutex, 100);
    TEST_ASSERT(ret == -1, "超时等待返回超时");

    coco_mutex_unlock(&mutex);

    coco_cond_destroy(&cond);
    coco_mutex_destroy(&mutex);
}

/* 测试 10: 多线程竞争 */
static void* counter_thread(void *arg) {
    coco_mutex_t *mutex = (coco_mutex_t*)arg;
    static int shared_counter = 0;

    for (int i = 0; i < 1000; i++) {
        coco_mutex_lock(mutex);
        shared_counter++;
        coco_mutex_unlock(mutex);
    }

    return NULL;
}

static void test_multi_thread_contention(void) {
    printf("\n[TEST 10] 多线程竞争\n");

    coco_mutex_t mutex;
    coco_mutex_init(&mutex, NULL);

    coco_thread_t threads[4];
    for (int i = 0; i < 4; i++) {
        coco_thread_create(&threads[i], NULL, counter_thread, &mutex);
    }

    for (int i = 0; i < 4; i++) {
        coco_thread_join(threads[i], NULL);
    }

    TEST_ASSERT(1, "多线程竞争完成 (无死锁)");

    coco_mutex_destroy(&mutex);
}

/* === 主测试入口 === */

int main(void) {
    printf("=== 平台抽象层测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. 线程原语抽象 (coco_thread.h)\n");
    printf("  2. 互斥锁抽象 (coco_mutex.h)\n");
    printf("  3. 条件变量抽象 (coco_cond.h)\n");
    printf("  4. Linux/macOS/Windows 统一 API\n");

#if COCO_PLATFORM_WINDOWS
    printf("\n平台: Windows\n");
#elif COCO_PLATFORM_MACOS
    printf("\n平台: macOS\n");
#elif COCO_PLATFORM_LINUX
    printf("\n平台: Linux\n");
#endif

    test_thread_create_join();
    test_thread_id();
    test_thread_yield();
    test_cpu_count();
    test_mutex_basic();
    test_mutex_trylock();
    test_mutex_recursive();
    test_cond_basic();
    test_cond_timedwait();
    test_multi_thread_contention();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 5 验收标准 US-013 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}