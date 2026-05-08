/**
 * time.c - 快速时间获取函数
 *
 * 使用 CPU 时间戳计数器 (TSC) 提供纳秒级时间测量。
 * x86-64: rdtsc 指令
 * ARM64: cntvct_el0 寄存器
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif

/**
 * 获取快速时间戳（纳秒）
 *
 * 使用 CPU 时间戳计数器，比 clock_gettime 更快。
 * 适用于时间片检查等高频调用场景。
 *
 * @return 当前时间（纳秒）
 */
uint64_t coco_get_time_fast(void) {
#if defined(__x86_64__) || defined(_M_X64)
    /* x86-64: 使用 rdtsc 指令 */
    uint64_t tsc = __rdtsc();

    /* 获取 TSC 频率（首次调用时缓存） */
    static uint64_t tsc_freq = 0;
    static bool freq_initialized = false;

    if (!freq_initialized) {
        /* 使用 clock_gettime 测量 TSC 频率 */
        struct timespec ts1, ts2;
        uint64_t tsc1, tsc2;

        clock_gettime(CLOCK_MONOTONIC, &ts1);
        tsc1 = __rdtsc();

        /* 等待 1ms 以获得足够的精度 */
        struct timespec sleep_ts = {0, 1000000};  /* 1ms */
        nanosleep(&sleep_ts, NULL);

        clock_gettime(CLOCK_MONOTONIC, &ts2);
        tsc2 = __rdtsc();

        uint64_t elapsed_ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL
                            + (ts2.tv_nsec - ts1.tv_nsec);
        if (elapsed_ns > 0) {
            tsc_freq = (tsc2 - tsc1) * 1000000000ULL / elapsed_ns;
        } else {
            /* 回退到估算值 (假设 ~3GHz) */
            tsc_freq = 3000000000ULL;
        }
        freq_initialized = true;
    }

    /* 转换 TSC 到纳秒 */
    return tsc * 1000000000ULL / tsc_freq;

#elif defined(__aarch64__) || defined(_M_ARM64)
    /* ARM64: 使用 cntvct_el0 寄存器 */
    uint64_t cntvct;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cntvct));

    /* 获取计数器频率 */
    static uint64_t cntfrq = 0;
    static bool freq_initialized = false;

    if (!freq_initialized) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
        if (cntfrq == 0) {
            /* 回退到 1GHz */
            cntfrq = 1000000000ULL;
        }
        freq_initialized = true;
    }

    /* 转换计数器值到纳秒 */
    return cntvct * 1000000000ULL / cntfrq;

#else
    /* 回退到 clock_gettime */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}
