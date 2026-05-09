# coco 优化方案

> 基于生产可用性分析，制定 5 阶段优化路线图。

---

## 总览

| Phase | 主题 | 优先级 | 预计工作量 | 目标 |
|-------|------|--------|-----------|------|
| 1 | 关键 Bug 修复 | P0 | 1-2 周 | 消除已知崩溃/内存错误 |
| 2 | 多线程调度器优化 | P0 | 2-3 周 | MT 调度器达到生产级 |
| 3 | 测试基础设施 | P0 | 1-2 周 | 内存安全验证 + 压力测试 |
| 4 | CI/CD 与 DevOps | P1 | 1 周 | 自动化构建、测试、发布 |
| 5 | API 稳定性与文档 | P1 | 1-2 周 | 版本管理 + 用户友好 |

---

## Phase 1: 关键 Bug 修复 (P0)

### 1.1 stack_pool_multi 对齐 Bug

**问题**: `coro_go.c:111` — `stack_pool_multi_free` 传入原始 `stack_size` 而非 `actual_stack_size`，与分配时 size class 不匹配。

```c
// 当前 (错误)
stack_pool_multi_free(pool, stack_top, stack_size);

// 应改为
stack_pool_multi_free(pool, stack_top, actual_stack_size);
```

**风险**: 内存泄漏 / 双重释放

**测试**: 添加 MT 模式下的协程创建/销毁循环测试

### 1.2 信号抢占死锁

**问题**: 异步信号可能在持有 `pthread_mutex` 期间触发，导致信号处理程序尝试获取同一锁时死锁。

**方案**: 信号处理程序中阻塞的信号集应包含所有可能触发抢占的关键路径。

```c
// 关键路径加锁前屏蔽 SIGUSR1/2
sigset_t old_set;
sigfillset(&old_set);
pthread_sigmask(SIG_BLOCK, &preempt_sigset, &old_set);
// ... critical section ...
pthread_sigmask(SIG_SETMASK, &old_set, NULL);
```

**影响文件**: `src/channel/channel.c`, `src/sched/global_sched.c`, `src/io/event_loop.c`

### 1.3 全局 TLS 变量跨线程误用

**问题**: `g_current_sched` / `g_current_coro` 是 `_Thread_local`，用户误跨线程调用会导致 NULL 解引用。

**方案**: 添加断言和错误返回

```c
#define ENSURE_IN_CORO() do { \
    if (!g_current_coro) { \
        fprintf(stderr, "coco: %s called outside coroutine\n", __func__); \
        return COCO_ERROR_INVALID; \
    } \
} while(0)
```

### 1.4 Channel 关闭竞态

**问题**: `coco_channel_close` 和 `coco_channel_send/recv` 之间的 `closed` 字段检查无锁保护。

**方案**: 将 `closed` 改为 atomic，所有检查使用 `atomic_load`。

### 1.5 select_best_p 无锁遍历

**问题**: `coro_go.c:22-44` 遍历所有 P 的 `local_runq_size` 无锁保护，高并发下读取到不一致值。

**方案**: 将 `local_runq_size` 改为 `atomic_uint32_t`。

---

## Phase 2: 多线程调度器优化 (P0)

### 2.1 Thundering Herd 修复

**当前**: `pthread_cond_broadcast` 唤醒所有空闲线程。

**目标**: 使用 `pthread_cond_signal` 按需唤醒，或实现 work-stealing 唤醒策略。

```c
// 方案: 根据队列长度决定唤醒数量
int to_wake = MIN(global_runq_size, idle_count);
for (int i = 0; i < to_wake; i++) {
    pthread_cond_signal(&idle_cond);
}
```

### 2.2 全局队列无锁化

**当前**: `pthread_mutex` 保护全局队列。

**目标**: 使用 Michael-Scott 无锁队列或分段锁。

**收益**: 高负载下全局队列吞吐量提升 3-5x

### 2.3 工作窃取优化

**当前**: 随机起点 + 线性扫描，最多尝试 3 次。

**改进**:
- 实现批量窃取（一次偷多个，降低后续冲突）
- 自适应窃取频率（基于失败次数退避）
- 添加窃取统计用于调优

### 2.4 P 绑定与迁移

**问题**: 协程创建后无法迁移到其他 P，可能导致负载不均衡。

**方案**: 实现轻量级 P 迁移机制：
- 当本地队列超过阈值时，将尾部协程推入全局队列
- 定期负载均衡检查（现有 `schedule_balanced` 未集成到调度循环）

### 2.5 Netpoller 集成完善

**当前**: 专用 netpoller 线程与 worker 线程协作。

**待完善**:
- I/O 就绪通知机制（使用 eventfd 唤醒 worker）
- 跨 P 的 I/O 就绪协程分发
- 连接关闭时的资源清理时序

---

## Phase 3: 测试基础设施 (P0)

### 3.1 AddressSanitizer (ASan)

**目标**: 检测内存错误（越界、UAF、泄漏）。

```cmake
# CMakeLists.txt 新增
option(COCO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(COCO_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()
```

**运行**: `cmake -DCOCO_ENABLE_ASAN=ON -B build-asan && cmake --build build-asan && cd build-asan && ctest`

### 3.2 ThreadSanitizer (TSan)

**目标**: 检测多线程数据竞争。

```cmake
option(COCO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(COCO_ENABLE_TSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()
```

**重点**: MT 调度器、Channel MT 模式、Netpoller

### 3.3 Valgrind 内存泄漏检测

```bash
valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./test_all
```

**关注点**:
- 协程销毁后栈内存回收
- Timer 销毁后回调参数释放
- Channel 销毁后等待节点清理

### 3.4 压力测试

| 测试名称 | 场景 | 验证目标 |
|----------|------|----------|
| `stress_coro_count` | 10万协程创建/销毁 | 内存不泄漏、无崩溃 |
| `stress_channel_burst` | 1000协程并发 channel 通信 | 无死锁、数据完整 |
| `stress_io_thunder` | 1万连接同时 accept | 无文件描述符泄漏 |
| `stress_timer_wheel` | 10万定时器同时创建/取消 | 内存/时间正确 |
| `stress_mt_scaling` | MT 调度器从 1P 扩展到 8P | 线性扩展比 > 0.7x |
| `stress_long_running` | 持续运行 24 小时 | 内存不增长 |

### 3.5 Fuzz 测试

使用 AFL++ 或 libFuzzer 对以下入口进行模糊测试：
- `coco_channel_send/recv` 参数
- `coco_channel_select` case 数组
- `coco_create` stack_size 参数（极端值）
- Timer 参数（0, 溢出值, 负数）

### 3.6 Benchmark 回归

```bash
# 每次 PR 运行基准测试
./bench_switch    # 目标 < 100ns
./bench_channel   # 记录 ops/sec
./bench_preempt   # 目标 p99 <= 15ms
```

---

## Phase 4: CI/CD 与 DevOps (P1)

### 4.1 GitHub Actions 配置

```yaml
# .github/workflows/ci.yml
name: CI
on: [push, pull_request]

jobs:
  build-test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
        arch: [x86_64]
        sanitizer: [none, asan, tsan]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DCOCO_ENABLE_COVERAGE=${{ matrix.sanitizer == 'none' }}
      - name: Build
        run: cmake --build build
      - name: Test
        run: cd build && ctest --output-on-failure
```

### 4.2 覆盖率门槛

- 单元测试覆盖率 >= 80%
- PR 新增代码覆盖率 >= 90%
- 覆盖率报告通过 codecov 上传

### 4.3 发布流程

```
tag → CI 构建 → 测试通过 → 创建 GitHub Release
     → 构建产物: libcoco.a + coco.h
     → 自动生成 CHANGELOG
```

### 4.4 跨平台构建矩阵

| OS | Arch | 状态 |
|----|------|------|
| Ubuntu 22.04 | x86_64 | ✅ 当前 |
| Ubuntu 22.04 | ARM64 | 🔄 待添加 |
| macOS 13 | x86_64 | 🔄 待添加 |
| macOS 13 | ARM64 | 🔄 待添加 |
| Windows 2022 | x86_64 | ⏳ Phase 1 完成后 |

---

## Phase 5: API 稳定性与文档 (P1)

### 5.1 版本策略

采用语义化版本 (SemVer)：
- `MAJOR`: 不兼容 API 变更
- `MINOR`: 向后兼容功能新增
- `PATCH`: 向后兼容 Bug 修复

当前 v2.0.0，下一个稳定版本建议 v2.1.0（包含 Phase 1-2 修复）。

### 5.2 ABI 稳定性

- 公共头文件标记 `__attribute__((visibility("default")))` 符号
- 内部符号标记 `__attribute__((visibility("hidden")))`
- 添加 `coco_version()` 函数返回库版本
- 发布时附带符号导出列表文件

### 5.3 文档完善

| 文档 | 状态 | 优先级 |
|------|------|--------|
| API 参考文档 (Doxygen) | 部分完成 | 高 |
| 最佳实践指南 | 缺失 | 高 |
| 性能调优指南 | 缺失 | 中 |
| 故障排查手册 | 缺失 | 中 |
| 迁移指南 (v1 → v2) | 已完成 | - |

### 5.4 Windows 支持完成

**当前状态**: WSAPoll I/O backend 为 stub。

**待完成**:
1. 完成 `src/platform/windows/preempt.c`（Windows 不支持 POSIX 信号，需用 APC 或 Fiber）
2. 实现 `poll_windows.c` 完整功能
3. 在 Windows 上跑通所有单元测试
4. README 更新状态为 "Supported"

---

## 实施建议

### 执行顺序

```
Phase 1 (Bug 修复) → Phase 3 (测试) → Phase 2 (MT 优化) → Phase 4 (CI/CD) → Phase 5 (API/文档)
```

**理由**:
1. 先修 Bug 确保代码正确性
2. 用测试验证修复效果，防止回归
3. 在有测试保护下安全重构 MT 调度器
4. CI/CD 保证持续质量
5. API 稳定性在功能稳定后固化

### 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|----------|----------|
| M1: Bug Free | ASan/TSan 零错误 | 3-4 周 |
| M2: MT Ready | 压力测试通过，万级协程稳定 | 5-7 周 |
| M3: Production Ready | CI 全绿，覆盖率 > 80% | 8-9 周 |
| M4: v3.0 Release | 全平台测试通过，文档完整 | 10-11 周 |
