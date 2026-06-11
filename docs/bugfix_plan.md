# coco Bug 修复方案

> 基于代码审查发现的问题，制定针对性修复计划。

## 发现的问题汇总

### Bug 1: channel.c `coco_channel_close` 竞态条件（高优先级）

**问题描述：**
`coco_channel_close` (channel.c:568) 在设置 `closed=1` 后直接遍历 `send_wait_head`/`recv_wait_head` 等等待队列，没有持有 `wait_queue_lock`。但 `coco_channel_send`/`recv` 在入队等待时（line 392, 526）持有 `wait_queue_lock`。

**竞态场景：**
1. 线程 A 在 `send` 中检查 `closed` (atomic_load)，看到 0
2. 线程 B 在 `close` 中设置 `closed=1` (atomic_exchange)
3. 线程 B 开始遍历 `send_wait_head`，此时 `send_wait_head` 可能是 NULL 或包含一些 waiter
4. 线程 A 获取 `wait_queue_lock`，将新 waiter 加入 `send_wait_head`
5. 线程 B 继续遍历，可能错过这个新 waiter（导致该 waiter 永远阻塞）
6. 更严重的是，如果线程 B 在遍历过程中，线程 A 同时修改链表，可能导致链表损坏

**修复方案：**
在 `coco_channel_close` 中持 `wait_queue_lock` 进行所有等待队列操作：
```c
void coco_channel_close(coco_channel_t *ch) {
    if (!ch || atomic_exchange_explicit(&ch->closed, 1, memory_order_acq_rel)) {
        return;
    }
    coco_sched_t *sched = g_current_sched;

    pthread_mutex_lock(&ch->wait_queue_lock);
    /* 在锁保护下遍历和唤醒所有等待者 */
    while (ch->recv_wait_head) { ... }
    while (ch->recv_select_head) { ... }
    while (ch->send_wait_head) { ... }
    while (ch->send_select_head) { ... }
    pthread_mutex_unlock(&ch->wait_queue_lock);
}
```

**验证：** 运行 `test_channel`, `stress_channel_burst`, `test_cancel` 等测试。

---

### Bug 2: channel_mt.c `closed` 不是 atomic（高优先级）

**问题描述：**
`channel_mt.c:28`：`ch->closed = 0;` 和 `ch->closed = 1;` 中的 `closed` 是普通 `int`，但在多线程环境中可能被多个线程并发访问。

**影响文件：**
- `channel_mt.c:28` - 创建时初始化
- `channel_mt.c:109, 154, 173, 230, 249, 336, 393, 412` - 读写 closed
- `channel_mt.h` - 结构定义

**修复方案：**
1. 在 `channel_mt.h` 中将 `int closed;` 改为 `_Atomic int closed;`
2. 在所有读写 `closed` 的地方使用 atomic 操作：
   - `ch->closed` → `atomic_load_explicit(&ch->closed, memory_order_acquire)`
   - `ch->closed = 1` → `atomic_exchange_explicit(&ch->closed, 1, memory_order_acq_rel)`
   - `ch->closed = 0` → `atomic_init(&ch->closed, 0)` 或 `atomic_store_explicit`
3. 创建时 `atomic_init(&ch->closed, 0);`

**验证：** 运行 `test_channel_mt`, `stress_channel_mt_burst`。

---

### Bug 3: channel_mt.c 缺少信号抢占屏蔽（高优先级）

**问题描述：**
`channel_mt.c` 中所有 `pthread_mutex_lock(&ch->lock)` 的区域都没有 `coco_preempt_block_signal()`/`coco_preempt_unblock_signal()` 保护。如果抢占信号在持有 mutex 时触发，信号处理程序可能尝试获取同一锁或调用 `schedule_ready`，导致死锁或嵌套信号问题。

**影响范围：**
- `coco_channel_mt_send` (line 107)
- `coco_channel_mt_recv` (line 171)
- `coco_channel_mt_send_thread` (line 247)
- `coco_channel_mt_recv_thread` (line 285)
- `coco_channel_mt_try_send` (line 334)
- `coco_channel_mt_try_recv` (line 372)
- `coco_channel_mt_close` (line 410)
- `coco_channel_mt_destroy` (line 70)
- `select_dequeue_all_mt` (line 521)

**修复方案：**
在每个 `pthread_mutex_lock(&ch->lock)` 前添加 `coco_preempt_block_signal();`，在 `pthread_mutex_unlock(&ch->lock)` 后添加 `coco_preempt_unblock_signal();`。

**注意：** 
- `trylock` 分支也要处理（如果 trylock 失败，不需要 unblock，因为没有 block）
- `select_dequeue_all_mt` 中的 trylock 循环也要处理
- `select_timeout_cb_mt` 是定时器回调，在定时器线程中执行，不需要信号屏蔽

**验证：** 运行带抢占的测试 `test_preempt_channel`, `stress_channel_mt_burst`。

---

### Bug 4: coco_destroy 在 MT 模式下不释放栈（中优先级）

**问题描述：**
在 `coro_go.c` 中创建的协程，没有设置 `coro->is_exclusive = true`。导致 `coco_destroy` 中：
```c
if (coro->is_exclusive && coro->stack_base) { ... }
```
条件不成立，栈内存泄漏。

同样，`coco_sched_destroy` 中遍历 coro_table 时，对于 MT 模式下的协程，用的是 `stack_pool_free` 而非 `stack_pool_multi_free`。

**修复方案：**
1. 在 `coro_go.c` 中设置 `coro->is_exclusive = true;`
2. 修改 `coco_destroy` 以支持 `stack_pool_multi_free`：
   ```c
   if (coro->is_exclusive && coro->stack_base) {
       if (coro->stack_pool) {
           // MT 模式：使用 stack_pool_multi_free
           stack_pool_multi_free((stack_pool_multi_t *)coro->stack_pool,
                                 coro->stack_top, coro->stack_size);
       } else if (sched && sched->stack_pool) {
           // 单线程模式：使用 stack_pool_free
           stack_pool_free(sched->stack_pool, coro->stack_top, coro->stack_size);
       } else {
           coco_stack_free(coro->stack_top, coro->stack_size);
       }
       coro->stack_base = NULL;
   }
   ```
3. 修改 `coco_sched_destroy` 中相应的释放逻辑

**验证：** 运行 `test_coco_go`, `test_global_sched`, `stress_mt_scaling`。

---

### Bug 5: coco_sched_destroy 对 MT 协程的栈释放错误（中优先级）

**问题描述：**
`coco_sched_destroy` (coro.c:191) 遍历 `sched->coro_table` 释放协程栈。但 MT 模式下，这些协程的栈是从 P 的 `stack_pool_multi` 分配的，不是从 `sched->stack_pool` 分配的。

**修复方案：**
在 `coco_sched_destroy` 中检查 `coro->stack_pool`：
```c
if (coro->is_exclusive && coro->stack_base) {
    if (coro->stack_pool) {
        // MT 模式
        stack_pool_multi_free((stack_pool_multi_t *)coro->stack_pool,
                              coro->stack_top, coro->stack_size);
    } else if (sched->stack_pool) {
        stack_pool_free(sched->stack_pool, coro->stack_top, coro->stack_size);
    } else {
        coco_stack_free(coro->stack_top, coro->stack_size);
    }
    coro->stack_base = NULL;
}
```

**验证：** 运行 `test_sched_reinit`, `test_global_sched`。

---

## 实施计划

### 任务划分

| 任务 | Bug | 文件 | 复杂度 |
|------|-----|------|--------|
| Task A | Bug 1 | src/channel/channel.c | 低 |
| Task B | Bug 2 + Bug 3 | src/channel/channel_mt.c, src/channel/channel_mt.h | 中 |
| Task C | Bug 4 + Bug 5 | src/core/coro.c, src/core/coro_go.c | 中 |

### 依赖关系

- Task A, B, C 之间无依赖，可以并行执行
- 所有任务完成后需要运行完整测试套件验证

### 测试验证

修复完成后运行：
```bash
cmake -B build && cmake --build build
cd build && ctest --output-on-failure
```

特别关注以下测试：
- `test_channel` - 验证 channel.c 修复
- `test_channel_mt` - 验证 channel_mt.c 修复
- `test_coco_go` - 验证 MT 协程创建和销毁
- `test_global_sched` - 验证全局调度器
- `stress_channel_burst` - 压力测试
- `stress_channel_mt_burst` - MT 压力测试
- `stress_mt_scaling` - MT 扩展性测试
