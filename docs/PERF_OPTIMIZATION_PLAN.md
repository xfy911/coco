# Coco 性能优化计划

> 基于 v2.2.0 基准测试与代码分析制定
> 日期: 2026-06-11

---

## 1. 现状与基线

### 1.1 Benchmark 结果

| 测试项 | 当前值 | 目标值 | 差距 |
|--------|--------|--------|------|
| Context switch (`bench_switch`) | **14.71 ns** | < 100 ns | 已达标 |
| Hot stack yield (`bench_hot_stack`) | **907 ns** | < 200 ns | **4.5x 差距** |
| Hot/cold switch (`bench_hot_cold_switch`) | **295 ns** | < 100 ns | **3x 差距** |
| Channel ops (`bench_channel`) | **6.66 M ops/sec** | > 10 M | 1.5x 差距 |
| I/O throughput (`bench_io`) | **48.9 MB/s** | > 200 MB/s | **4x 差距** |
| MT sched speedup (`bench_mt_sched`) | **1.00x (4P vs 1P)** | > 3.0x | **严重** |
| Stack pool alloc p99 (`bench_stack`) | **17.57 μs** | < 5 μs | 3.5x 差距 |
| Preempt checkpoint | **1.30 ns** | < 10 ns | 已达标 |

### 1.2 关键瓶颈定位

```
┌─────────────────────────────────────────────────────────────┐
│                    性能瓶颈优先级矩阵                        │
├─────────────────────────────────────────────────────────────┤
│ P0: MT 调度器扩展性崩溃 (4P=1.0x)                           │
│     └─ 全局队列锁竞争 + yield 全局入队 + 窃取效率低          │
│ P1: 热栈切换路径 (907ns vs 14ns)                            │
│     └─ LRU 操作 + 栈备份/恢复 + 状态检查                     │
│ P1: I/O 吞吐量 (48.9 MB/s)                                  │
│     └─ 重复非阻塞设置 + netpoller 开销 + 取消检查            │
│ P2: 栈池分配 (p99 17.57μs)                                  │
│     └─ 全局 mutex + 无 per-thread cache                      │
│ P2: Channel 吞吐量 (6.66M ops/s)                            │
│     └─ 等待队列操作 + 内存分配                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Phase 1: MT 调度器扩展性优化 (P0)

**目标**: 4P speedup 从 1.00x → > 3.0x

### 2.1 问题根因分析

`bench_mt_sched` 测试 100 个协程各 yield 1000 次：
- `coco_go()` 将协程放入某个 P 的本地队列（`select_best_p()` 选择队列最短的 P）
- 协程运行后调用 `coco_yield()` → 放入**全局队列**（`coco_global_runq_put()`）
- 其他 P 的 worker 从全局队列获取协程 → 需要竞争 `global_runq_lock`
- 这导致：
  1. **100K 次全局队列锁竞争**（100 coros × 1000 yields）
  2. 协程在 P 之间"跳来跳去"，破坏 cache locality
  3. `find_runnable()` 窃取后逐个 `runq_put()`，带来二次锁竞争

### 2.2 优化方案 A: 协程本地亲和性（推荐）

**核心思想**: yield 后优先回到原 P 的本地队列，而非全局队列。

```c
// 当前 (问题)
int coco_yield(void) {
    // ...
    if (gs && gs->processor_count > 0) {
        coco_global_runq_put(coro);  // 全局队列！
    }
    // ...
}

// 优化后
int coco_yield(void) {
    // ...
    if (gs && gs->processor_count > 0) {
        coco_processor_t *p = get_current_p();
        if (p && runq_put(p, coro) != 0) {
            // 本地队列满才放入全局
            coco_global_runq_put(coro);
        }
    }
    // ...
}
```

**预期收益**: 消除 90%+ 的全局队列操作，4P speedup 提升至 2.5x+

**实现要点**:
- `get_current_p()` 返回当前 worker 绑定的 P
- `runq_put()` 需要处理本地队列满时溢出到全局队列（已有逻辑）
- 保持 `schedule_ready()`（I/O/定时器唤醒）仍使用全局队列，确保公平性

### 2.3 优化方案 B: 全局队列无锁化

**核心思想**: 使用 Michael-Scott 无锁队列替代 mutex 保护的全局队列。

```c
// 使用原子指针 CAS 操作
typedef struct _Atomic(coco_coro_t *) ms_queue_node_t;

typedef struct {
    _Atomic(coco_coro_t *) head;
    _Atomic(coco_coro_t *) tail;
} ms_queue_t;
```

**预期收益**: 高并发下全局队列吞吐量提升 3-5x
**风险**: 复杂度较高，需仔细验证 ABA 问题

### 2.4 优化方案 C: 批量工作窃取

**核心思想**: `runq_steal()` 一次偷多个，`find_runnable()` 直接消费 batch 而非逐个回插。

```c
// 当前: steal 后逐个 runq_put()
coco_coro_t *g = runq_steal(target);
if (g) {
    coco_coro_t *next = g->next;
    while (next) {
        runq_put(p, next);  // 每次都要锁！
        next = next->next;
    }
    return g;
}

// 优化: 直接将 batch 挂到本地队列尾部（持有锁一次性插入）
coco_coro_t *batch = runq_steal_batch(target, &count);
if (batch) {
    runq_put_batch(p, batch, count);  // 一次性入队，只锁一次
    return batch;
}
```

**预期收益**: 减少窃取后的锁竞争 50%+

### 2.5 Phase 1 实施计划

| 任务 | 改动文件 | 工作量 | 验证测试 |
|------|----------|--------|----------|
| A1: yield 本地亲和性 | `src/core/coro.c` | 1d | `bench_mt_sched`, `stress_mt_scaling` |
| A2: 修复 batch 窃取入队 | `src/sched/sched.c`, `src/sched/runq.c` | 1d | `bench_mt_sched`, `test_global_sched_mt` |
| B: 全局队列无锁化 (可选) | `src/sched/global_sched.c` | 3d | 全部 MT 测试 |
| C: 窃取统计与自适应 | `src/sched/sched.c` | 0.5d | `bench_mt_sched` |

---

## 3. Phase 2: 热栈与 Yield 路径优化 (P1)

**目标**: hot stack yield 从 907ns → < 200ns

### 3.1 问题根因分析

```
coco_yield() 热点路径:
  1. hot_lru_move_to_head()      ~ 50-100ns  (链表指针修改)
  2. state 原子读取              ~ 10ns
  3. 全局/本地队列入队           ~ 100-500ns (取决于锁竞争)
  4. coco_ctx_switch()           ~ 15ns
  ─────────────────────────────────
  合计                           ~ 200-600ns (不含 schedule 开销)
```

`bench_hot_stack` 测的是 8 个协程在 8 个热槽位中 yield：
- 每次 yield 都要 `hot_lru_move_to_head()`
- 单线程模式下 `enqueue_ready()` 是 O(1) 链表操作，但热栈路径还有额外检查

### 3.2 优化方案 A: 惰性 LRU 更新

**核心思想**: 不是所有 yield 都需要更新 LRU，只有可能被驱逐时才更新。

```c
// 当前: 每次 yield 都 move to head
if (!coro->is_exclusive && coro->hot_slot_idx >= 0) {
    hot_lru_move_to_head(sched, &coro->hot_node);
}

// 优化: 每 N 次 yield 更新一次，或仅在槽位紧张时更新
if (!coro->is_exclusive && coro->hot_slot_idx >= 0) {
    if (++coro->hot_yield_count % 16 == 0) {
        hot_lru_move_to_head(sched, &coro->hot_node);
    }
}
```

**预期收益**: 减少 90% 的 LRU 操作，yield 降低 ~100ns

### 3.2 优化方案 B: 无锁本地队列

**核心思想**: 单线程调度器的 `enqueue_ready()` / `dequeue_ready()` 已经很快了（O(1) 链表），但 MT 模式下 `runq_put()` 有 mutex。如果实现无锁的 Treiber stack 或bounded SPSC queue 作为本地队列：

```c
// Treiber stack (无锁 LIFO) for local runq
typedef struct {
    _Atomic(coco_coro_t *) head;
} lockfree_stack_t;
```

**注意**: 需要区分 LIFO（无锁）和 FIFO（当前实现）。对于 fairness 不敏感的场景，LIFO 有更好的 cache locality。

**预期收益**: 本地队列入队/出队从 ~50ns → ~10ns

### 3.3 Phase 2 实施计划

| 任务 | 改动文件 | 工作量 | 验证测试 |
|------|----------|--------|----------|
| A: 惰性 LRU | `src/core/coro.c`, `src/core/hot_stack.c` | 0.5d | `bench_hot_stack`, `bench_hot_cold_switch` |
| B: 无锁本地队列 (原型) | `src/sched/runq.c` | 2d | `bench_switch`, `bench_mt_sched` |
| C: yield 快速路径 | `src/core/coro.c` | 0.5d | `bench_hot_stack` |

---

## 4. Phase 3: I/O 与 Netpoller 优化 (P1)

**目标**: I/O throughput 从 48.9 MB/s → > 200 MB/s

### 4.1 问题根因分析

`bench_io` 使用 socket pair 测试 4 个协程对：
- 每次 `coco_read()` / `coco_write()` 都调用 `coco_poll_set_nonblock(fd)` → `fcntl()` 系统调用！
- 虽然 fd 已经是非阻塞的，`fcntl()` 仍被调用
- MT 模式下还要经过 netpoller 注册/注销

### 4.2 优化方案 A: 缓存非阻塞状态

**核心思想**: 避免重复的 `fcntl()` 调用。

```c
// 当前
void coco_poll_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);  // 每次 I/O 都 syscall!
    if (flags != -1 && !(flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

// 优化: 使用 fd 表缓存非阻塞状态
void coco_poll_set_nonblock(int fd) {
    if (fd_table_is_nonblock(fd)) return;  // 快速路径，无 syscall
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1 && !(flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    fd_table_mark_nonblock(fd);
}
```

**预期收益**: 消除 90% 的 `fcntl()` 调用，I/O 吞吐量提升 2-3x

### 4.3 优化方案 B: 批量 I/O 提交

**核心思想**: 将多个小 I/O 合并为一次提交（特别是 io_uring）。

已有 `coco_batch_begin/end` API，但仅限 io_uring。可扩展为通用批量接口：

```c
coco_batch_io_t *batch = coco_batch_begin();
coco_batch_add_read(batch, fd1, buf1, size1);
coco_batch_add_read(batch, fd2, buf2, size2);
coco_batch_submit(batch, results, 2);
coco_batch_end(batch);
```

**预期收益**: 小 I/O 场景下吞吐量提升 5-10x

### 4.4 优化方案 C: 零拷贝发送

**核心思想**: 对于文件发送，使用 `sendfile()` 替代 `read()` + `write()`。

```c
ssize_t coco_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
```

**预期收益**: 文件传输场景减少 50% 内存拷贝

### 4.5 Phase 3 实施计划

| 任务 | 改动文件 | 工作量 | 验证测试 |
|------|----------|--------|----------|
| A: O_NONBLOCK 缓存 | `src/io/fd_table.c`, `src/io/poll_*.c` | 0.5d | `bench_io`, `bench_io_backend` |
| B: 批量 I/O 通用化 | `src/io/event_loop.c`, `src/io/poll_iouring.c` | 2d | `bench_io`, `test_batch_io` |
| C: sendfile 封装 | `src/io/event_loop.c` | 0.5d | 新增 benchmark |
| D: netpoller 批量唤醒 | `src/io/netpoller_mt.c` | 1d | `stress_io_thunder` |

---

## 5. Phase 4: 栈池与内存分配优化 (P2)

**目标**: stack pool alloc p99 从 17.57μs → < 5μs

### 5.1 问题根因分析

`stack_pool_multi_alloc()`:
1. `pthread_mutex_lock(&pool->lock)` — 全局锁
2. 遍历 freelist 找到合适 size class
3. `pthread_mutex_unlock(&pool->lock)`

高并发下，多个 worker 线程竞争同一个 per-P 栈池锁。

### 5.2 优化方案 A: Per-thread 栈缓存

**核心思想**: 每个线程缓存 1-2 个常用 size 的栈，避免锁竞争。

```c
typedef struct {
    void *cache[STACK_POOL_MULTI_NUM_CLASSES];
} stack_pool_cache_t;

static _Thread_local stack_pool_cache_t tl_cache;

void *stack_pool_multi_alloc_fast(stack_pool_multi_t *pool, size_t size) {
    int class = stack_pool_multi_get_class_index(size);
    if (class >= 0 && tl_cache.cache[class]) {
        void *stack = tl_cache.cache[class];
        tl_cache.cache[class] = NULL;
        return stack;
    }
    return stack_pool_multi_alloc(pool, size);  // slow path
}
```

**预期收益**: 缓存命中时分配从 ~10μs → ~10ns，p99 显著降低

### 5.3 优化方案 B: 预分配热栈

**核心思想**: 调度器启动时预分配一批常用 size 的栈。

```c
// 在 coco_processor_create() 中预分配
for (int i = 0; i < 4; i++) {
    void *stack = mmap(...);  // 预分配 4 个 64KB 栈
    stack_pool_preload(p->stack_pool, stack, STACK_SIZE_64K);
}
```

**预期收益**: 消除启动阶段的 mmap 开销

### 5.4 Phase 4 实施计划

| 任务 | 改动文件 | 工作量 | 验证测试 |
|------|----------|--------|----------|
| A: Per-thread cache | `src/core/stack_pool_multi.c` | 1d | `bench_stack`, `stress_mt_scaling` |
| B: 预分配热栈 | `src/core/stack_pool_multi.c`, `src/sched/global_sched.c` | 0.5d | `bench_stack` |
| C: 批量释放 | `src/core/stack_pool_multi.c` | 0.5d | `bench_stack` |

---

## 6. Phase 5: Channel 吞吐量优化 (P2)

**目标**: channel ops 从 6.66M → > 10M ops/sec

### 6.1 问题根因分析

Channel send/recv 路径：
1. 检查对方等待队列（无锁快速路径）
2. 检查缓冲区（有缓冲时）
3. 创建等待节点，入队，yield

对于无缓冲 channel，每次 send/recv 都要：
- 分配/释放等待节点（虽然可能是嵌入的）
- 修改等待队列（链表操作）
- yield + resume（context switch）

### 6.2 优化方案 A: 批量 Channel 操作

```c
// 批量发送，减少上下文切换次数
int coco_channel_send_batch(coco_channel_t *ch, void **vals, int n);
int coco_channel_recv_batch(coco_channel_t *ch, void **vals, int n, int *received);
```

### 6.3 优化方案 B: MPMC 无锁 Channel（多线程）

对于 `channel_mt.c`，使用 ring buffer + CAS 实现无锁操作：

```c
typedef struct {
    _Atomic(uint32_t) head;
    _Atomic(uint32_t) tail;
    void *buffer[];
} mpmc_ring_t;
```

### 6.4 Phase 5 实施计划

| 任务 | 改动文件 | 工作量 | 验证测试 |
|------|----------|--------|----------|
| A: 批量 Channel API | `src/channel/channel.c` | 1d | `bench_channel` |
| B: MPMC 无锁原型 | `src/channel/channel_mt.c` | 3d | `bench_channel`, `stress_channel_mt_burst` |

---

## 7. 实施路线图

```
Week 1-2: Phase 1 (MT 扩展性)
  ├─ Day 1-2: yield 本地亲和性
  ├─ Day 3-4: batch 窃取优化
  └─ Day 5-7: 验证 + 调参

Week 3: Phase 2 (热栈 + Yield)
  ├─ Day 1-2: 惰性 LRU
  ├─ Day 3-4: 无锁本地队列原型
  └─ Day 5: 验证

Week 4: Phase 3 (I/O)
  ├─ Day 1-2: O_NONBLOCK 缓存 + sendfile
  ├─ Day 3-4: 批量 I/O 通用化
  └─ Day 5: 验证

Week 5: Phase 4 + 5 (栈池 + Channel)
  ├─ Day 1-2: per-thread cache
  ├─ Day 3: 批量 Channel
  └─ Day 4-5: 验证 + 回归测试

Week 6: 收尾
  ├─ 全量 benchmark 对比
  ├─ Sanitizer 验证 (ASan/TSan/UBSan)
  └─ 文档更新
```

---

## 8. 验证标准

每项优化完成后必须满足：

1. **Benchmark 回归**: 相关 benchmark 提升达到预期
2. **功能测试**: `ctest --output-on-failure` 全绿
3. **Sanitizer  clean**:
   ```bash
   cmake -B build-asan -DCOCO_ENABLE_ASAN=ON && cmake --build build-asan && cd build-asan && ctest
   cmake -B build-tsan -DCOCO_ENABLE_TSAN=ON && cmake --build build-tsan && cd build-tsan && ctest
   ```
4. **压力测试**: `stress_mt_scaling`, `stress_channel_burst`, `stress_io_thunder` 通过

---

## 9. 预期最终收益

| 指标 | 当前 | 目标 | 提升 |
|------|------|------|------|
| MT 4P speedup | 1.00x | > 3.0x | **3x** |
| Hot stack yield | 907 ns | < 200 ns | **4.5x** |
| I/O throughput | 48.9 MB/s | > 200 MB/s | **4x** |
| Stack pool p99 | 17.57 μs | < 5 μs | **3.5x** |
| Channel ops | 6.66 M/s | > 10 M/s | **1.5x** |

> **关键胜利**: MT 调度器扩展性和 I/O 吞吐量是生产环境最关心的指标。
