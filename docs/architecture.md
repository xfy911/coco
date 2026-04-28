# coco 架构设计文档

> 版本: 1.0 | 更新日期: 2026-04-28
>
> 本文档基于源码实际实现撰写，所有引用均标注文件路径和行号。

---

## 1. 整体架构

coco 是一个生产级 C 协程库，采用**有栈协程 (stackful coroutine)** + **协作式调度**模型，支持 Linux/macOS/Windows 三平台 (x86-64 / ARM64)。核心由五大模块组成：

```
+-------------------------------------------------------------+
|                        用户代码                              |
|  coco_create / coco_yield / coco_channel_send / coco_read   |
+-------------------------------------------------------------+
         |              |              |              |
    +----+-----+  +-----+----+  +-----+-----+  +----+-----+
    |  协程     |  | Channel  |  |   I/O     |  |  Timer   |
    |  Coro    |  |          |  |           |  |  时间轮   |
    +----+-----+  +-----+----+  +-----+-----+  +----+-----+
         |              |              |              |
    +----+--------------+--------------+--------------+----+
    |                   调度器 Scheduler                     |
    |  run queue (双向链表)  |  协程池 (ID 映射表)           |
    |  main_ctx             |  poll_fd (epoll/kqueue)       |
    +----+------------------+---------------+--------------+
         |                                  |
    +----+-----------+              +-------+--------+
    | 上下文切换 (汇编) |              | I/O 多路复用    |
    | coco_ctx_switch  |              | epoll / kqueue  |
    +------------------+              +----------------+
         |
    +----+-----------+
    | 栈管理          |
    | mmap + guard   |
    +----------------+
```

**模块职责**：

| 模块 | 源文件 | 职责 |
|------|--------|------|
| 调度器 | `src/core/coro.c` | 运行队列管理、调度循环、协程生命周期 |
| 协程 | `src/core/coro.c` + `src/core/context.c` | 协程创建/退出/yield、上下文初始化 |
| Channel | `src/channel/channel.c` | CSP 模型通信、缓冲/无缓冲、等待队列 |
| I/O | `src/io/event_loop.c` + `src/io/poll_*.c` | 异步 I/O 封装、epoll/kqueue 集成 |
| Timer | `src/timer/timer_wheel.c` | 4 层时间轮、定时器注册/触发 |

**关键设计决策**：
- 调度器逻辑集中在 `coro.c`，`sched.c` 仅为占位（`src/core/sched.c:2`）
- 全局单线程模型：`g_current_sched` 和 `g_current_coro` 为全局指针（`src/core/coro.c:11-12`）
- 协程入口统一由 `coro_entry_wrapper` 包装，确保退出时自动调用 `coco_exit`（`src/core/coro.c:15-21`）

---

## 2. 调度流程

### 2.1 运行队列

调度器使用双向链表作为运行队列 (run queue)：

```
sched->ready_head                              sched->ready_tail
     |                                               |
     v                                               v
+---------+    +---------+    +---------+       +---------+
| coro_A  |<-->| coro_B  |<-->| coro_C  |<--...| coro_N  |
+---------+    +---------+    +---------+       +---------+
     ^                                               ^
     |                                               |
   dequeue                                          enqueue
  (从头部取)                                        (从尾部加)
```

**数据结构**（`src/coco_internal.h:57-61`）：

```c
struct coco_sched {
    coco_coro_t *ready_head;   /* 队列头 */
    coco_coro_t *ready_tail;   /* 队列尾 */
    uint32_t ready_count;      /* 队列中协程数 */
    ...
};
```

**入队 enqueue_ready**（`src/core/coro.c:24-36`）：
- 将协程状态设为 `COCO_STATE_READY`
- 插入到链表尾部，O(1) 操作

**出队 dequeue_ready**（`src/core/coro.c:122-137`）：
- 从链表头部取出，O(1) 操作
- 维护 head/tail 指针和 prev 指针

### 2.2 主调度循环

```
coco_sched_run()
  |
  v
+---------------------------------------------------+
| while (coro_count > 0) {                          |
|                                                   |
|   1. 处理就绪队列                                  |
|      while (ready_count > 0) {                    |
|          coro = dequeue_ready()                   |
|          switch_to_coro(coro)  ---+               |
|          handle_coro_return() <---+               |
|      }                                            |
|                                                   |
|   2. 处理定时器                                    |
|      coco_timer_tick()                            |
|      if (ready_count > 0) continue;               |
|                                                   |
|   3. 等待 I/O 事件                                |
|      coco_poll_wait(1ms)   // epoll/kqueue       |
|      OR usleep(1ms)        // 无 fd 注册时        |
| }                                                  |
+---------------------------------------------------+
```

源码位置：`src/core/coro.c:183-221`

**关键点**：
- 协程运行完毕后控制权返回调度器，由 `handle_coro_return` 处理状态（`src/core/coro.c:155-181`）
- 无就绪协程时，先检查定时器再等 I/O，优先响应定时事件
- I/O 等待使用 1ms 超时，避免纯 channel 场景下死锁（`src/core/coro.c:212`）

### 2.3 Yield/Resume 机制

**yield 流程**（`src/core/coro.c:299-314`）：

```
协程调用 coco_yield()
  |
  v
检查协程状态是否为 RUNNING
  |-- 是: enqueue_ready(sched, coro)   // 重新入队
  |-- 否: 不入队 (已被 channel 等设为 WAITING)
  |
  v
coco_ctx_switch(&coro->ctx, &sched->main_ctx)   // 切回调度器
```

**resume 流程**（`src/core/coro.c:140-152`）：

```
调度器 switch_to_coro()
  |
  v
g_current_coro = coro
coro->state = RUNNING
  |
  v
coco_set_overflow_checkpoint()    // 设置栈溢出恢复点
  |-- 返回 0: coco_ctx_switch(&sched->main_ctx, &coro->ctx)
  |-- 返回 1: 从栈溢出恢复 (coro 状态已由 handler 设置)
```

**注意**：`coco_yield` 仅在协程处于 `RUNNING` 状态时才入队。这保证了 channel send/recv 等操作在设置 `WAITING` 状态后 yield 不会误将协程重新放回就绪队列（`src/core/coro.c:308`）。

---

## 3. 内存模型

### 3.1 栈分配

使用 `mmap` 分配栈内存，并在底部设置 guard page 检测溢出。

**栈布局**（`src/core/stack.c:31-37`）：

```
高地址
+-------------------+  <- stack_base + size + page_size
|                   |     (coco_stack_alloc 返回值 = stack_top)
|   可用栈空间       |     协程在此区域运行，栈从高地址向低地址增长
|                   |
+-------------------+  <- stack_base + page_size (guard page 边界)
|   Guard Page      |     mprotect(PROT_NONE)，访问触发 SIGSEGV
+-------------------+  <- stack_base (mmap 返回地址)
低地址
```

**分配过程**（`src/core/stack.c:38-69`）：

1. 将请求的 size 向上对齐到页大小（`stack.c:42`）
2. 总分配 = size + page_size（`stack.c:45`）
3. `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` 分配匿名内存（`stack.c:48-55`）
4. `mprotect(base, page_size, PROT_NONE)` 保护 guard page（`stack.c:62`）
5. 返回栈顶地址 `base + total_size`（`stack.c:68`）

**释放过程**（`src/core/stack.c:77-89`）：
- 从 stack_top 反算 stack_base：`stack_top - (size + page_size)`
- `munmap` 一次性释放全部内存

### 3.2 协程结构体布局

**coco_coro_t**（`src/coco_internal.h:30-50`）：

```
offset  字段           说明
------  ----           ----
0       id             协程唯一 ID (uint64_t)
8       state          协程状态 (coco_state_t 枚举)
~       ctx            上下文 (coco_ctx_t, 见第 7 节)
~       stack_base     栈起始地址 (guard page 起始)
~       stack_top      栈顶地址 (可用栈最高地址)
~       stack_size     栈大小 (不含 guard page)
~       entry          入口函数指针
~       arg            入口参数
~       result         返回值
~       next/prev      调度链表双向指针
~       wait_fd        等待的 fd (-1 表示无)
~       wake_time      定时唤醒时间 (ns)
~       error_cb       错误回调函数
```

**协程 ID 映射**：调度器维护 `coro_table[]` 数组，以 ID 为索引直接映射（`src/coco_internal.h:63`），最大容量 `COCO_MAX_COROUTINES = 10000`（`include/coco.h:48`）。

### 3.3 栈溢出检测

使用 `SIGSEGV` handler + `sigaltstack` 实现栈溢出检测与恢复（`src/core/signal.c`）：

1. 初始化时设置 64KB 替代信号栈（`signal.c:22`）
2. 注册 `SA_SIGINFO | SA_ONSTACK` 的 SIGSEGV handler（`signal.c:134`）
3. 调度器切换协程前设置 `sigsetjmp` 恢复点（`signal.c:181`）
4. handler 从 `si_addr` 反查溢出协程（`signal.c:34-65`）
5. 标记协程为 `COCO_STATE_OVERFLOW`，`siglongjmp` 回到调度器（`signal.c:97`）

---

## 4. Channel 实现

### 4.1 数据结构

**Channel 结构**（`src/channel/channel.c:23-38`）：

```
coco_channel_t
+----------------------------------+
| capacity      缓冲区大小 (0=无缓冲) |
| closed        是否已关闭           |
|                                  |
| [有缓冲] 环形缓冲区:              |
|   buffer       void** 数组        |
|   head         读位置             |
|   tail         写位置             |
|   count        当前元素数          |
|                                  |
| send_wait_head/tail  发送等待队列  |
| recv_wait_head/tail  接收等待队列  |
+----------------------------------+
```

**等待队列节点**（`src/channel/channel.c:16-20`）：

```c
typedef struct wait_node {
    coco_coro_t *coro;    // 等待的协程
    void *value;           // 待传递的数据
    struct wait_node *next;
} wait_node_t;
```

### 4.2 无缓冲 Channel

无缓冲 channel (capacity = 0) 实现同步传递 (rendezvous)：

```
发送者                              接收者
  |                                   |
  | coco_channel_send(ch, value)      |
  |   检查 recv_wait_head             |
  |   |-- 有接收者在等待:              |
  |   |   直接传递 value              |
  |   |   enqueue_ready(recv_coro)   |
  |   |   返回成功                     |
  |   |                               |
  |   |-- 无接收者:                    |
  |       创建 send_wait_node          |
  |       state = WAITING             |
  |       coco_yield()  ------+       |
  |                           |       |
  |     (协程挂起)             |       |
  |                           |       |
  |                    coco_channel_recv(ch, &value)
  |                      检查 send_wait_head
  |                      |-- 有发送者在等待:
  |                          value = send_node->value
  |                          enqueue_ready(send_coro)
  |                          返回成功
  |                                   |
  | <---- (发送者被唤醒，继续执行) -----+
```

源码位置：send 在 `src/channel/channel.c:109-158`，recv 在 `src/channel/channel.c:167-231`

### 4.3 有缓冲 Channel

有缓冲 channel 使用环形缓冲区，当缓冲区未满时 send 立即返回，缓冲区未空时 recv 立即返回。

**send 流程**（`src/channel/channel.c:109-158`）：

```
coco_channel_send(ch, value)
  |
  |-- 检查是否有 recv 等待者 -> 直接传递 (同无缓冲)
  |
  |-- 缓冲区未满 (count < capacity):
  |     buffer[tail] = value
  |     tail = (tail + 1) % capacity
  |     count++
  |     返回成功
  |
  |-- 缓冲区已满:
        阻塞等待 (同无缓冲的无接收者情况)
```

**recv 流程**（`src/channel/channel.c:167-231`）：

```
coco_channel_recv(ch, &value)
  |
  |-- 缓冲区有数据 (count > 0):
  |     value = buffer[head]
  |     head = (head + 1) % capacity
  |     count--
  |     若有 send 等待者: 将其 value 放入缓冲区并唤醒
  |     返回成功
  |
  |-- 缓冲区空:
  |     检查是否有 send 等待者 (无缓冲场景) -> 直接传递
  |     否则阻塞等待
```

### 4.4 等待队列

等待队列使用 FIFO 单链表，头尾指针实现 O(1) 入队出队：

- `enqueue_wait`：添加到尾部（`channel.c:52-60`）
- `dequeue_wait`：从头部取出（`channel.c:63-73`）

### 4.5 Channel 关闭

`coco_channel_close`（`src/channel/channel.c:238-263`）：
1. 标记 `closed = 1`
2. 唤醒所有 recv 等待者（它们恢复后会收到 `COCO_ERROR_CHANNEL_CLOSED`）
3. 唤醒所有 send 等待者（同上）

### 4.6 节点释放策略

等待节点采用**双重释放**策略避免 use-after-free：
- 直接传递模式（send 找到 recv 或反之）：唤醒对方协程，**不释放**节点，由发送者恢复后自行释放（`channel.c:127-128`）
- 发送者阻塞后恢复：在 yield 返回后释放自己的等待节点（`channel.c:150`）
- 接收者阻塞后恢复：在 yield 返回后释放自己的等待节点（`channel.c:228`）

---

## 5. I/O 模型

### 5.1 架构

```
+-----------------------------------------------+
|              用户代码                           |
|  coco_read / coco_write / coco_accept / ...   |
+-----------------------------------------------+
          |                    |
    +-----+------+     +------+------+
    | poll_linux |     | poll_macos  |
    |  epoll     |     |  kqueue     |
    +-----+------+     +------+------+
          |                    |
          +--------+-----------+
                   |
          +--------+--------+
          | coco_poll_wait   |
          | coco_poll_register |
          | coco_poll_unregister |
          +-----------------+
                   |
          +--------+--------+
          |  调度器          |
          |  poll_fd         |
          |  g_fd_table[]    |
          +-----------------+
```

### 5.2 平台实现

**Linux - epoll**（`src/io/poll_linux.c`）：
- 使用 `epoll_create1(EPOLL_CLOEXEC)` 创建实例（`poll_linux.c:34`）
- 注册时设置 `EPOLLONESHOT | EPOLLET`（边缘触发 + 一次性）（`poll_linux.c:78`）
- FD 自动设为非阻塞（`poll_linux.c:70-74`）
- fd 到协程的映射使用 `g_fd_table[1024]` 全局数组（`poll_linux.c:15`）

**macOS - kqueue**（`src/io/poll_macos.c`）：
- 使用 `kqueue()` 创建实例（`poll_macos.c:36`）
- 注册时使用 `EV_ADD | EV_ONESHOT`（`poll_macos.c:82`）
- 注销时需分别删除 EVFILT_READ 和 EVFILT_WRITE（`poll_macos.c:108-112`）

**Windows**（`src/io/poll_windows.c`）：当前为占位实现。

### 5.3 协程 I/O 流程

以 `coco_read` 为例（`src/io/poll_macos.c:165-189`）：

```
coco_read(fd, buf, count)
  |
  v
while (1) {
    n = read(fd, buf, count)
    |
    |-- n >= 0:  返回读取字节数
    |
    |-- errno == EAGAIN:
    |     coco_poll_register(sched, fd, coro, POLLIN)
    |       |-- fd 设为非阻塞
    |       |-- 注册到 epoll/kqueue
    |       |-- g_fd_table[fd] = coro
    |       |-- coro->wait_fd = fd
    |       |-- coro->state = WAITING
    |
    |     coco_yield()  // 切回调度器，等待事件
    |
    |     (I/O 就绪，调度器唤醒协程)
    |     coco_poll_unregister(sched, fd)
    |     g_fd_table[fd] = NULL
    |     coro->wait_fd = -1
    |
    |-- 其他错误: 返回 COCO_ERROR
}
```

### 5.4 事件分发

`coco_poll_wait`（Linux: `poll_linux.c:116-138`，macOS: `poll_macos.c:124-155`）：

1. 调用 `epoll_wait` / `kevent` 等待就绪事件
2. 遍历就绪事件，通过 `g_fd_table[fd]` 找到关联协程
3. 检查协程状态为 `WAITING`，则 `enqueue_ready` 唤醒
4. 清除 fd 映射，重置 `coro->wait_fd = -1`

### 5.5 sleep 实现

`coco_sleep`（`src/io/event_loop.c:11-26`）将定时器与协程结合：

```
coco_sleep(ms)
  |
  v
coco_timer_add(timer_wheel, ms, coro)   // 注册定时器关联协程
coro->state = COCO_STATE_WAITING
coco_yield()                             // 挂起协程
                                         // 定时器到期后，timer_tick 唤醒协程
```

---

## 6. 时间轮

### 6.1 四层分级结构

采用 4 层层级时间轮 (hierarchical timing wheel)，类似内核的 HRTIMER 实现。

**结构定义**（`src/timer/timer_wheel.c:21-33`）：

```
W1 (第1层)         W2 (第2层)          W3 (第3层)          W4 (第4层)
+-----------+     +-----------+      +-----------+      +-----------+
| 256 slots |     | 64 slots  |      | 64 slots  |      | 64 slots  |
| 1ms 精度  |     | 256ms 精度|      | 16384ms   |      | 1048576ms |
| 0-255ms   |     | 0-16383ms |      | 0-1048575 |      | 0-6710863 |
+-----------+     +-----------+      +-----------+      +-----------+
  w1_tick            w2_tick            w3_tick            w4_tick
```

**层级关系**：

| 层级 | 槽位数 | 精度 | 范围 |
|------|--------|------|------|
| W1 | 256 | 1 ms | 0 - 255 ms |
| W2 | 64 | 256 ms | 0 - 16,383 ms (~16s) |
| W3 | 64 | 16,384 ms | 0 - 1,048,575 ms (~17min) |
| W4 | 64 | 1,048,576 ms | 0 - 67,108,863 ms (~18.6h) |

### 6.2 定时器放置

`place_timer`（`src/timer/timer_wheel.c:125-145`）根据延迟选择层级：

```
delay_ms
  |
  |-- < 256:         W1, slot = (w1_tick + delay) % 256
  |-- < 16384:       W2, slot = (w2_tick + delay/256) % 64
  |-- < 1048576:     W3, slot = (w3_tick + delay/16384) % 64
  |-- >= 1048576:    W4, slot = (w4_tick + delay/1048576) % 64
```

每个槽位维护一个定时器链表（同 hash 链表，头插法）。

### 6.3 Tick 驱动与 Cascading

`coco_timer_tick`（`src/timer/timer_wheel.c:257-288`）：

```
coco_timer_tick(tw, sched)
  |
  v
计算 elapsed = now_ms - current_time_ms
  |
  for (i = 0; i < elapsed; i++) {
      current_time_ms++
      w1_tick++
      |
      |-- w1_tick % 256 == 0 ?
      |     w2_tick++
      |     cascade_timers(level=2)    // W2 当前槽位定时器降级到 W1
      |     |
      |     |-- w2_tick % 64 == 0 ?
      |           w3_tick++
      |           cascade_timers(level=3)
      |           |
      |           |-- w3_tick % 64 == 0 ?
      |                 w4_tick++
      |                 cascade_timers(level=4)
      |
      process_expired_timers()         // 处理 W1 当前槽位
  }
```

**Cascading 过程**（`src/timer/timer_wheel.c:194-230`）：
1. 取出上层的当前槽位链表
2. 将链表中的每个定时器重新调用 `place_timer`
3. 由于时间推进，定时器会降级到更低的层级

**过期处理**（`src/timer/timer_wheel.c:233-254`）：
1. 取出 W1 当前槽位的定时器链表
2. 若定时器关联了协程 (`timer->coro`)，则 `enqueue_ready` 唤醒
3. 若定时器有回调 (`timer->callback`)，直接调用
4. 释放定时器内存

### 6.4 定时器类型

两种定时器创建方式（`src/timer/timer_wheel.c`）：

| API | 位置 | 关联 | 用途 |
|-----|------|------|------|
| `coco_timer(delay, callback, arg)` | 行 148 | 回调 | 通用定时器 |
| `coco_timer_add(tw, delay, coro)` | 行 168 | 协程 | 协程 sleep 等 |

---

## 7. 上下文切换

### 7.1 上下文结构

`coco_ctx_t`（`src/coco_internal.h:13-27`）保存 callee-saved 寄存器：

```
offset  ARM64 字段    x86-64 对应    说明
-----   ----------    ------------   ----
0       sp            rsp            栈指针
8       fp (x29)      rbp            帧指针
16      lr (x30)      (未使用)       链接寄存器
24      x19           rbx            callee-saved
32      x20           r12
40      x21           r13
48      x22           r14
56      x23           r15
64      x24           (padding)
72      x25
80      x26
88      x27
96      x28
```

**注意**：x86-64 和 ARM64 共享同一个 `coco_ctx_t` 结构，但实际使用的字段不同。x86-64 只使用 offset 0-48 (sp, rbp, rbx, r12-r15)，ARM64 使用全部字段。

### 7.2 x86-64 汇编实现

**System V ABI (Linux/macOS)**（`src/platform/linux/ctx_x86_64.S`，`src/platform/macos/ctx_x86_64.S`）：

callee-saved 寄存器约定：`rbx, rbp, r12-r15`（caller-saved 寄存器 `rax, rcx, rdx, rsi, rdi, r8-r11` 不需要保存）。

**coco_ctx_save**（保存当前上下文）：
```asm
# rdi = coco_ctx_t*
mov [rdi + 0],  rsp      # offset 0:  sp
mov [rdi + 8],  rbp      # offset 8:  fp/rbp
mov [rdi + 16], rbx      # offset 16: x19/rbx
mov [rdi + 24], r12      # offset 24: x20/r12
mov [rdi + 32], r13      # offset 32: x21/r13
mov [rdi + 40], r14      # offset 40: x22/r14
mov [rdi + 48], r15      # offset 48: x23/r15
xor eax, eax             # 返回 0（首次保存）
ret
```

**coco_ctx_load**（加载目标上下文）：
```asm
# rdi = coco_ctx_t*
mov rsp, [rdi + 0]       # 恢复 sp
mov rbp, [rdi + 8]       # 恢复 rbp
mov rbx, [rdi + 16]      # 恢复 rbx
mov r12, [rdi + 24]      # 恢复 r12
mov r13, [rdi + 32]      # 恢复 r13
mov r14, [rdi + 40]      # 恢复 r14
mov r15, [rdi + 48]      # 恢复 r15
mov eax, 1               # 返回 1（切换成功）
ret
```

**coco_ctx_switch**（原子保存并切换）：
```asm
# rdi = current_ctx*, rsi = target_ctx*
# 保存当前上下文到 [rdi]
mov [rdi + 0],  rsp
mov [rdi + 8],  rbp
mov [rdi + 16], rbx
mov [rdi + 24], r12
mov [rdi + 32], r13
mov [rdi + 40], r14
mov [rdi + 48], r15

# 加载目标上下文从 [rsi]
mov rsp, [rsi + 0]
mov rbp, [rsi + 8]
mov rbx, [rsi + 16]
mov r12, [rsi + 24]
mov r13, [rsi + 32]
mov r14, [rsi + 40]
mov r15, [rsi + 48]

ret                       # 跳转到目标上下文的返回地址
```

### 7.3 ARM64 汇编实现

**AAPCS64 约定**（`src/platform/macos/ctx_arm64.S`）：

callee-saved 寄存器：`x19-x28, x29(fp), x30(lr)`。使用 `stp` 成对存储/加载。

**coco_ctx_switch**：
```asm
# x0 = current_ctx*, x1 = target_ctx*
# 保存当前上下文
stp x19, x20, [x0, #24]
stp x21, x22, [x0, #40]
stp x23, x24, [x0, #56]
stp x25, x26, [x0, #72]
stp x27, x28, [x0, #88]
stp fp, lr, [x0, #8]         # fp=x29, lr=x30
mov x2, sp
str x2, [x0, #0]             # 保存 sp

# 加载目标上下文
ldp x19, x20, [x1, #24]
ldp x21, x22, [x1, #40]
ldp x23, x24, [x1, #56]
ldp x25, x26, [x1, #72]
ldp x27, x28, [x1, #88]
ldp fp, lr, [x1, #8]
ldr x2, [x1, #0]
mov sp, x2                   # 恢复 sp

ret                          # 跳转到 lr（目标上下文的返回地址）
```

### 7.4 上下文初始化

`coco_ctx_init`（`src/core/context.c:27-54`）在协程创建时构造初始栈帧，使协程首次被 resume 时能正确跳转到入口函数。

**ARM64 栈帧构造**：

```
stack_top
  |
  v
+-------------------+
|   16B padding     |  对齐
+-------------------+
|   lr = entry      |  p[2]: 返回地址 = 入口函数
+-------------------+
|   x0 = arg        |  p[1]: 参数
+-------------------+
|   fp = 0          |  p[0]: 帧指针初始值（栈底标记）
+-------------------+
|   x19 = 0         |
|   x20 = 0         |
|   ...             |
|   x28 = 0         |  <- sp (ctx->sp 指向此处)
+-------------------+
```

**关键细节**（`context.c:29-43`）：
- 预留 112 字节（14 个 64-bit 值）
- 16 字节对齐（`sp &= ~0xF`）
- `ctx->lr = entry`：当 `coco_ctx_load` 执行 `ret` 时，CPU 跳转到 `lr` 即入口函数
- `ctx->fp = 0`：帧指针初始为 0，作为栈回溯的终止标记

### 7.5 完整上下文切换流程

协程创建后首次 resume：

```
调度器                          协程栈
  |                               |
  coco_ctx_switch(main_ctx, coro_ctx)
  |                               |
  保存 main_ctx:                  |
    rsp, rbp, rbx, r12-r15       |
  |                               |
  加载 coro_ctx:                  |
    rsp = coro->ctx.sp  ------->  +-- sp 指向构造的栈帧
    rbp = 0 (fp)                  |
    lr = entry                    |
    x19-x28 = 0                   |
  |                               |
  ret -------> entry(arg)         |
              |                   |
              coro_entry_wrapper(arg)
              |                   |
              coco_self()         |
              coro->entry(arg)    |
              coco_exit()         |
                                  |
  <------ coco_ctx_switch(coro_ctx, main_ctx)
```

---

## 附录 A: 协程状态机

```
                    coco_create()
                        |
                        v
                 +-------------+
          +----->|   CREATED   |
          |      +-------------+
          |            |
          |            | enqueue_ready()
          |            v
          |      +-------------+
          |      |    READY    |<--------+
          |      +-------------+         |
          |            |                 |
          |            | switch_to_coro()|
          |            v                 |
          |      +-------------+         |
          |      |   RUNNING   |         |
          |      +-------------+         |
          |       /    |     \           |
          |      /     |      \          |
          |  yield()  exit()  wait      |
          |     |       |       \        |
          |     |       |    +---------+ |
          |     |       |    | WAITING | |
          |     |       |    +---------+ |
          |     |       |     |         |
          |     |       |     | I/O就绪  |
          |     |       |     | 定时到期  |
          |     |       |     | channel  |
          |     |       |     v         |
          |     |       |  enqueue_ready()
          |     |       v                |
          |     |  +-------------+       |
          |     |  |    DEAD     |       |
          |     |  +-------------+       |
          |     |                        |
          +-----+   (仅 yield 路径)      |
                coco_yield() 重新入队 ----+
```

**状态说明**（`include/coco.h:28-35`）：

| 状态 | 含义 | 转移条件 |
|------|------|----------|
| CREATED | 已创建未运行 | `coco_create` 后入队 |
| READY | 等待调度 | yield/I/O就绪/定时到期/channel唤醒 |
| RUNNING | 正在执行 | 调度器 switch_to_coro |
| WAITING | 等待事件 | I/O等待/channel等待/sleep |
| DEAD | 已结束 | `coco_exit` |
| OVERFLOW | 栈溢出 | SIGSEGV 在 guard page |

## 附录 B: 文件索引

| 文件 | 行数 | 核心内容 |
|------|------|----------|
| `include/coco.h` | 247 | 公开 API、错误码、状态枚举 |
| `src/coco_internal.h` | 110 | 内部结构体、内部 API 声明 |
| `src/core/coro.c` | 364 | 调度器 + 协程生命周期 |
| `src/core/sched.c` | 3 | 占位文件 |
| `src/core/context.c` | 54 | 上下文初始化 (C) |
| `src/core/stack.c` | 90 | mmap 栈分配 + guard page |
| `src/core/signal.c` | 182 | SIGSEGV 栈溢出检测 |
| `src/channel/channel.c` | 290 | Channel 实现 |
| `src/io/event_loop.c` | 26 | coco_sleep |
| `src/io/poll_linux.c` | 291 | epoll I/O 多路复用 |
| `src/io/poll_macos.c` | 308 | kqueue I/O 多路复用 |
| `src/io/poll_windows.c` | 5 | 占位文件 |
| `src/timer/timer_wheel.c` | 305 | 4 层时间轮 |
| `src/platform/linux/ctx_x86_64.S` | 78 | Linux x86-64 上下文切换 |
| `src/platform/linux/ctx_arm64.S` | 17 | Linux ARM64 (stub) |
| `src/platform/macos/ctx_x86_64.S` | 48 | macOS x86-64 上下文切换 |
| `src/platform/macos/ctx_arm64.S` | 52 | macOS ARM64 上下文切换 |
| `src/platform/windows/ctx_x86_64.S` | - | Windows x86-64 上下文切换 |
