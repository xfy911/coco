/**
 * test_all.c - 测试汇总程序
 *
 * 此文件不直接运行测试，而是提供测试列表和文档。
 * 实际测试通过 ctest 运行。
 */

#include <stdio.h>

int main(void) {
    printf("=== Coco 测试套件 ===\n\n");

    printf("运行测试:\n");
    printf("  cd build && ctest --output-on-failure\n\n");

    printf("运行基准测试:\n");
    printf("  ./build/bench_switch   - 上下文切换性能\n");
    printf("  ./build/bench_preempt  - 抢占延迟 (p99 <= 15ms)\n");
    printf("  ./build/bench_stack    - 栈增长开销 (< 1μs)\n\n");

    printf("单元测试列表:\n");
    printf("  test_coro            - 协程基本功能\n");
    printf("  test_channel         - Channel 通信\n");
    printf("  test_channel_select  - Channel select\n");
    printf("  test_timer           - 定时器\n");
    printf("  test_io              - I/O 操作\n");
    printf("  test_stack           - 栈管理\n");
    printf("  test_stack_growth    - 动态栈增长\n");
    printf("  test_stack_shrink    - 栈收缩\n");
    printf("  test_cancel          - 协程取消\n");
    printf("  test_priority        - 优先级调度\n");
    printf("  test_fairness        - 公平调度\n");
    printf("  test_preempt         - 异步抢占\n");
    printf("  test_global_sched    - 全局调度器\n");
    printf("  test_context_api     - 上下文 API\n");
    printf("  更多...\n\n");

    printf("使用 scripts/run_all_tests.sh 运行完整测试套件。\n");

    return 0;
}
