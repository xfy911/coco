# Coco Shared Stack (Hot Resident) Design

> **Status**: Implemented  
> **Date**: 2026-06-03  
> **Author**: OpenCode  
> **Scope**: Replace per-coroutine exclusive stacks with shared hot-resident stacks + lazy backup  

---

## 1. Overview

### 1.1 Problem Statement

Coco currently allocates **64KB+ per coroutine** via `mmap`. At 10,000 coroutines this consumes **640MB+** of physical memory, limiting concurrency density. The context switch performance (14.48ns) is already near hardware limits, so further optimization must focus on **memory efficiency** rather than shaving nanoseconds from the switch path.

### 1.2 Solution Summary

Replace per-coroutine exclusive stacks with **shared physical stacks** using a **hot-resident + lazy backup** strategy:

- **8 hot slots** per scheduler, each holding a **128KB physical stack**
- Coroutines whose stacks reside in a hot slot are "hot" — resumed with **zero memcpy**
- Coroutines not in a hot slot are "cold" — their stacks are backed up to dynamically allocated memory
- **1 slot is reserved** for `COCO_PRIORITY_HIGH` coroutines, the remaining 7 use pure LRU
- Coroutines needing **>100KB** automatically fall back to **exclusive stacks** with dynamic growth (64KB→8MB)

### 1.3 Design Goals

| Goal | Target |
|------|--------|
| Memory per 10K coroutines | < 50MB (from 640MB) |
| Memory per 1M coroutines | < 5GB (from 64GB) |
| Hot switch latency | Same as current: ~15ns (zero memcpy) |
| Cold switch latency | < 100ns (including backup/restore) |
| Backward compatibility | All existing APIs unchanged |
| Correctness | All existing tests pass; new tests for stack integrity |

---

## 2. Core Concepts

### 2.1 Memory Model

```
Per-Scheduler Memory Layout:
┌──────────────────────────────────────────────┐
│ Hot Slot 0 (128KB + guard page)              │
│ ┌─────────────────────────────────────────┐  │
│ │ [HIGH priority coro stack - reserved]   │  │
│ │ Stack top = slot_base + 128KB           │  │
│ │ Guard page below slot_base              │  │
│ └─────────────────────────────────────────┘  │
├──────────────────────────────────────────────┤
│ Hot Slot 1-7 (128KB + guard page each)       │
│ ┌─────────────────────────────────────────┐  │
│ │ [LRU-managed coro stacks]               │  │
│ │ Only ONE coro active per slot at a time │  │
│ └─────────────────────────────────────────┘  │
├──────────────────────────────────────────────┤
│ Cold Backup Pool                             │
│ ┌─────────────────────────────────────────┐  │
│ │ Coro A backup: 3KB (allocated on demand)│  │
│ │ Coro B backup: 8KB                      │  │
│ │ Coro C backup: NULL (never ran)         │  │
│ └─────────────────────────────────────────┘  │
├──────────────────────────────────────────────┤
│ Exclusive Stack Pool (fallback)              │
│ ┌─────────────────────────────────────────┐  │
│ │ Coro X: 256KB exclusive + dynamic grow  │  │
│ │ Coro Y: 512KB exclusive                 │  │
│ └─────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

### 2.2 Hot vs Cold States

| State | Location | Resume Cost | Description |
|-------|----------|-------------|-------------|
| **Hot** | Physical hot slot (128KB) | **0 memcpy** | Recently ran; stack intact |
| **Cold** | Heap-allocated backup | **1 memcpy** (restore) | Evicted from hot slot; backed up |
| **Exclusive** | Private `mmap` stack | **0 memcpy** (same as current) | Large-stack coro; bypasses hot system |

### 2.3 LRU Management

- **LRU Head**: Most recently executed hot coroutine
- **LRU Tail**: Least recently executed hot coroutine (eviction victim)
- **Slot 0**: Reserved for `COCO_PRIORITY_HIGH`. If no HIGH coro exists, Slot 0 participates in LRU.
- Operations are **O(1)**: doubly-linked list with head/tail pointers.

---

## 3. Data Structure Changes

### 3.1 New Types

```c
/* Hot stack slot */
typedef struct coco_hot_slot {
    void *stack_top;           /* High address (end of usable stack) */
    void *stack_base;          /* Low address (start of usable stack, above guard) */
    size_t stack_size;         /* 128KB */
    coco_coro_t *occupant;     /* Current coroutine, or NULL */
    bool in_use;               /* TRUE if occupied */
    bool reserved;             /* TRUE if Slot 0 (HIGH priority reserved) */
} coco_hot_slot_t;

/* LRU node embedded in coro */
typedef struct coco_hot_node {
    coco_coro_t *coro;
    struct coco_hot_node *next;
    struct coco_hot_node *prev;
} coco_hot_node_t;
```

### 3.2 Modified: `struct coco_sched`

```c
struct coco_sched {
    /* === Existing fields (unchanged) === */
    coco_coro_t *ready_head;
    coco_coro_t *ready_tail;
    uint32_t ready_count;
    coco_ctx_t main_ctx;
    int poll_fd;
    // ... all existing fields preserved ...
    
    /* === NEW: Hot stack management === */
    coco_hot_slot_t *hot_slots;       /* Array of 8 slots */
    int hot_slot_count;               /* Configurable, default 8 */
    
    coco_hot_node_t *hot_lru_head;    /* Most recent */
    coco_hot_node_t *hot_lru_tail;    /* Least recent (eviction victim) */
    int hot_coro_count;               /* Number of hot coroutines */
    
    uint64_t sched_tick;              /* Monotonic counter incremented per switch */
    
    /* === NEW: Exclusive stack fallback === */
    void *exclusive_pool;             /* Optional: pool for exclusive stacks */
    size_t exclusive_count;           /* Number of exclusive-stack coroutines */
};
```

### 3.3 Modified: `struct coco_coro`

```c
struct coco_coro {
    /* === Existing fields (unchanged) === */
    uint64_t id;
    coco_state_t state;
    coco_ctx_t ctx;
    void *stack_base;          /* For exclusive stacks; NULL for hot/cold */
    void *stack_top;           /* For exclusive stacks; computed for hot/cold */
    size_t stack_size;         /* For exclusive stacks; 0 for hot/cold */
    void (*entry)(void*);
    void *arg;
    void *result;
    coco_coro_t *next;
    coco_coro_t *prev;
    int wait_fd;
    uint64_t wake_time;
    coco_error_cb error_cb;
    coco_priority_t priority;
    // ... all existing fields preserved ...
    
    /* === NEW: Stack management === */
    void *stack_backup;           /* Cold backup buffer (NULL if never backed up) */
    size_t stack_backup_size;     /* Capacity of backup buffer */
    size_t stack_used;            /* Actual bytes used (watermark) */
    
    /* === NEW: Hot stack state === */
    int hot_slot_idx;             /* -1 = cold or exclusive; 0-7 = hot slot index */
    coco_hot_node_t hot_node;     /* Embedded LRU node */
    uint64_t last_run_tick;       /* sched_tick at last execution */
    
    /* === NEW: Exclusive stack flag === */
    bool is_exclusive;            /* TRUE if using private mmap stack */
};
```

---

## 4. Key Flows

### 4.1 Scheduler Creation (`coco_sched_create`)

```c
coco_sched_t *coco_sched_create(void) {
    coco_sched_t *sched = calloc(1, sizeof(*sched));
    if (!sched) return NULL;
    
    /* Initialize existing fields */
    // ... existing init ...
    
    /* === NEW: Initialize hot slots === */
    sched->hot_slot_count = COCO_HOT_SLOTS_DEFAULT;  /* 8 */
    sched->hot_slots = calloc(sched->hot_slot_count, sizeof(coco_hot_slot_t));
    if (!sched->hot_slots) {
        free(sched);
        return NULL;
    }
    
    for (int i = 0; i < sched->hot_slot_count; i++) {
        coco_hot_slot_t *slot = &sched->hot_slots[i];
        slot->stack_size = COCO_HOT_STACK_SIZE;  /* 128KB */
        
        /* Allocate physical stack with guard page */
        size_t page_size = get_page_size();
        size_t total = slot->stack_size + page_size;
        void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            /* Cleanup previously allocated slots */
            for (int j = 0; j < i; j++) {
                munmap(sched->hot_slots[j].stack_base - page_size,
                       sched->hot_slots[j].stack_size + page_size);
            }
            free(sched->hot_slots);
            free(sched);
            return NULL;
        }
        
        /* Guard page at low address */
        mprotect(base, page_size, PROT_NONE);
        
        slot->stack_base = (char*)base + page_size;
        slot->stack_top = (char*)slot->stack_base + slot->stack_size;
        slot->occupant = NULL;
        slot->in_use = false;
        slot->reserved = (i == 0);  /* Slot 0 reserved for HIGH priority */
    }
    
    sched->hot_lru_head = NULL;
    sched->hot_lru_tail = NULL;
    sched->hot_coro_count = 0;
    sched->sched_tick = 0;
    
    return sched;
}
```

### 4.2 Coroutine Creation (`coco_create`)

```c
coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*),
                         void *arg, size_t stack_size) {
    coco_coro_t *coro = calloc(1, sizeof(*coro));
    if (!coro) return NULL;
    
    coro->id = next_coro_id++;
    coro->state = COCO_STATE_CREATED;
    coro->entry = entry;
    coro->arg = arg;
    coro->wait_fd = -1;
    coro->priority = COCO_PRIORITY_NORMAL;
    
    /* === NEW: Initialize stack management === */
    coro->stack_backup = NULL;
    coro->stack_backup_size = 0;
    coro->stack_used = 0;
    coro->hot_slot_idx = -1;
    coro->hot_node.coro = coro;
    coro->hot_node.next = NULL;
    coro->hot_node.prev = NULL;
    coro->last_run_tick = 0;
    coro->is_exclusive = false;
    
    /* === MODIFIED: Context initialization === */
    /* For first execution, coro will land in Slot 0 */
    /* ctx_init sets up initial stack frame; actual SP adjusted on first resume */
    coco_ctx_init(&coro->ctx, sched->hot_slots[0].stack_top, entry, arg);
    
    coro->state = COCO_STATE_READY;
    enqueue_ready(sched, coro);
    
    return coro;
}
```

### 4.3 Yield (`coco_yield`)

```c
void coco_yield(void) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;
    
    assert(coro != NULL);
    assert(coro->state == COCO_STATE_RUNNING);
    
    /* === NEW: Record stack watermark === */
    void *current_sp;
#if defined(__x86_64__)
    __asm__ volatile ("mov %%rsp, %0" : "=r"(current_sp));
#elif defined(__aarch64__)
    __asm__ volatile ("mov %0, sp" : "=r"(current_sp));
#endif
    
    if (coro->is_exclusive) {
        /* Exclusive stack: watermark from private stack */
        coro->stack_used = (size_t)((char*)coro->stack_top - (char*)current_sp);
    } else {
        /* Hot slot: watermark from shared stack */
        assert(coro->hot_slot_idx >= 0);
        coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];
        coro->stack_used = (size_t)((char*)slot->stack_top - (char*)current_sp);
        
        /* === NEW: Check for exclusive fallback trigger === */
        if (coro->stack_used > COCO_HOT_STACK_THRESHOLD) {  /* 100KB */
            coro->flags |= COCO_FLAG_NEED_EXCLUSIVE;
        }
    }
    
    /* === CRITICAL: Do NOT backup on yield (hot resident) === */
    /* The coroutine's stack content remains intact in the hot slot */
    
    /* Update LRU position */
    if (!coro->is_exclusive && coro->hot_slot_idx >= 0) {
        hot_lru_move_to_head(sched, &coro->hot_node);
    }
    
    /* Existing yield logic */
    coro->state = COCO_STATE_READY;
    enqueue_ready(sched, coro);
    
    g_current_coro = NULL;
    coco_ctx_switch(&coro->ctx, &sched->main_ctx);
}
```

### 4.4 Resume / Switch to Coroutine (`switch_to_coro`)

```c
static void switch_to_coro(coco_sched_t *sched, coco_coro_t *coro) {
    sched->sched_tick++;
    coro->last_run_tick = sched->sched_tick;
    
    if (coro->is_exclusive) {
        /* === EXCLUSIVE STACK PATH === */
        /* Same as current implementation: direct context switch */
        coro->ctx.sp = (char*)coro->stack_top - coro->stack_used;
        g_current_coro = coro;
        coro->state = COCO_STATE_RUNNING;
        
        if (coco_set_overflow_checkpoint() == 0) {
            coco_ctx_switch(&sched->main_ctx, &coro->ctx);
        }
        return;
    }
    
    if (coro->hot_slot_idx >= 0) {
        /* === HOT COROUTINE: Zero memcpy === */
        coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];
        
        /* Update SP to current watermark position */
        coro->ctx.sp = (char*)slot->stack_top - coro->stack_used;
        
        /* Move to LRU head */
        hot_lru_move_to_head(sched, &coro->hot_node);
        
    } else {
        /* === COLD COROUTINE: Need to restore === */
        
        /* Check if needs exclusive fallback */
        if (coro->flags & COCO_FLAG_NEED_EXCLUSIVE) {
            coro_migrate_to_exclusive(sched, coro);
            /* Now coro is exclusive; retry */
            switch_to_coro(sched, coro);
            return;
        }
        
        coco_hot_slot_t *slot = hot_slot_acquire(sched, coro);
        
        /* Restore from backup */
        void *restore_addr = (char*)slot->stack_top - coro->stack_used;
        memcpy(restore_addr, coro->stack_backup, coro->stack_used);
        
        /* Update state */
        coro->hot_slot_idx = slot - sched->hot_slots;
        slot->occupant = coro;
        slot->in_use = true;
        coro->ctx.sp = restore_addr;
        
        /* Insert to LRU head */
        hot_lru_insert_head(sched, &coro->hot_node);
        sched->hot_coro_count++;
    }
    
    g_current_coro = coro;
    coro->state = COCO_STATE_RUNNING;
    
    if (coco_set_overflow_checkpoint() == 0) {
        coco_ctx_switch(&sched->main_ctx, &coro->ctx);
    }
}
```

### 4.5 Hot Slot Acquisition (`hot_slot_acquire`)

```c
static coco_hot_slot_t *hot_slot_acquire(coco_sched_t *sched, coco_coro_t *coro) {
    /* 1. Find free slot */
    for (int i = 0; i < sched->hot_slot_count; i++) {
        if (!sched->hot_slots[i].in_use) {
            return &sched->hot_slots[i];
        }
    }
    
    /* 2. All slots occupied: evict LRU tail */
    coco_hot_node_t *victim_node = sched->hot_lru_tail;
    assert(victim_node != NULL);
    
    coco_coro_t *victim = victim_node->coro;
    coco_hot_slot_t *slot = &sched->hot_slots[victim->hot_slot_idx];
    
    /* 2a. Backup victim's stack */
    void *victim_sp = (char*)slot->stack_top - victim->stack_used;
    
    if (victim->stack_backup_size < victim->stack_used) {
        void *new_backup = realloc(victim->stack_backup, victim->stack_used);
        if (!new_backup) {
            /* Fatal: cannot evict */
            return NULL;
        }
        victim->stack_backup = new_backup;
        victim->stack_backup_size = victim->stack_used;
    }
    
    memcpy(victim->stack_backup, victim_sp, victim->stack_used);
    
    /* 2b. Mark victim as cold */
    victim->hot_slot_idx = -1;
    hot_lru_remove(sched, victim_node);
    sched->hot_coro_count--;
    
    /* Slot is now free ( occupant cleared by caller ) */
    slot->occupant = NULL;
    /* slot->in_use stays true until new coro claims it */
    
    return slot;
}
```

### 4.6 Exclusive Stack Fallback (`coro_migrate_to_exclusive`)

```c
static int coro_migrate_to_exclusive(coco_sched_t *sched, coco_coro_t *coro) {
    assert(coro->hot_slot_idx >= 0);
    
    coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];
    
    /* 1. Allocate exclusive stack (64KB + guard page) */
    size_t page_size = get_page_size();
    size_t stack_size = COCO_DEFAULT_STACK_SIZE;  /* 64KB initially */
    size_t total = stack_size + page_size;
    
    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return COCO_ERROR_NOMEM;
    }
    
    mprotect(base, page_size, PROT_NONE);
    
    /* 2. Copy current hot stack content to exclusive stack */
    void *src = (char*)slot->stack_top - coro->stack_used;
    void *dst_top = (char*)base + total;
    void *dst = (char*)dst_top - coro->stack_used;
    
    memcpy(dst, src, coro->stack_used);
    
    /* 3. Release hot slot */
    slot->in_use = false;
    slot->occupant = NULL;
    hot_lru_remove(sched, &coro->hot_node);
    sched->hot_coro_count--;
    coro->hot_slot_idx = -1;
    
    /* 4. Set exclusive stack metadata */
    coro->is_exclusive = true;
    coro->stack_base = (char*)base + page_size;
    coro->stack_top = dst_top;
    coro->stack_size = stack_size;
    coro->ctx.sp = dst;
    coro->flags &= ~COCO_FLAG_NEED_EXCLUSIVE;
    
    /* 5. Free cold backup if any (no longer needed) */
    free(coro->stack_backup);
    coro->stack_backup = NULL;
    coro->stack_backup_size = 0;
    
    return COCO_OK;
}
```

### 4.7 Coroutine Exit (`coco_exit`)

```c
void coco_exit(coco_coro_t *coro, void *result) {
    coco_sched_t *sched = g_current_sched;
    
    coro->result = result;
    coro->state = COCO_STATE_DEAD;
    
    if (coro->is_exclusive) {
        /* Exclusive stack: munmap on destroy, not here */
    } else if (coro->hot_slot_idx >= 0) {
        /* Release hot slot */
        coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];
        slot->in_use = false;
        slot->occupant = NULL;
        hot_lru_remove(sched, &coro->hot_node);
        sched->hot_coro_count--;
        coro->hot_slot_idx = -1;
    }
    
    /* Free backup buffer */
    free(coro->stack_backup);
    coro->stack_backup = NULL;
    
    g_current_coro = NULL;
    coco_ctx_switch(&coro->ctx, &sched->main_ctx);
}
```

---

## 5. Multi-Threading Support

### 5.1 Thread Safety

Each `coco_sched_t` (per worker thread) has its own hot slots. No cross-thread sharing of hot slots.

```
Thread 0 (P0)          Thread 1 (P1)          Thread 2 (P2)
┌─────────────┐        ┌─────────────┐        ┌─────────────┐
│ Hot Slots   │        │ Hot Slots   │        │ Hot Slots   │
│ [8 × 128KB] │        │ [8 × 128KB] │        │ [8 × 128KB] │
│ LRU: A,B,C  │        │ LRU: D,E    │        │ LRU: F      │
└─────────────┘        └─────────────┘        └─────────────┘
```

### 5.2 Work-Stealing Migration

When a coroutine is stolen from P0 to P1:

```c
static void coro_migrate_prepare(coco_sched_t *from, coco_coro_t *coro) {
    if (coro->is_exclusive) {
        /* Exclusive stacks are self-contained; no action needed */
        return;
    }
    
    if (coro->hot_slot_idx >= 0) {
        /* Force backup before migration */
        coco_hot_slot_t *slot = &from->hot_slots[coro->hot_slot_idx];
        void *src = (char*)slot->stack_top - coro->stack_used;
        
        if (coro->stack_backup_size < coro->stack_used) {
            coro->stack_backup = realloc(coro->stack_backup, coro->stack_used);
            coro->stack_backup_size = coro->stack_used;
        }
        
        memcpy(coro->stack_backup, src, coro->stack_used);
        
        /* Release slot */
        slot->in_use = false;
        slot->occupant = NULL;
        hot_lru_remove(from, &coro->hot_node);
        from->hot_coro_count--;
        coro->hot_slot_idx = -1;
    }
    /* If already cold, nothing to do */
}
```

---

## 6. Error Handling

| Scenario | Handling |
|----------|----------|
| Stack usage > 128KB on hot slot | Trigger `COCO_FLAG_NEED_EXCLUSIVE`; migrate on next resume |
| Stack usage > 128KB on exclusive | Existing `stack_grow.c` handles dynamic expansion |
| Guard page hit (SIGSEGV) | Existing overflow handler: mark OVERFLOW, longjmp to scheduler |
| Backup realloc fails during eviction | Return NULL from `hot_slot_acquire`; scheduler handles gracefully |
| Exclusive mmap fails | Return `COCO_ERROR_NOMEM`; coro remains in hot slot (may hit guard page) |
| Work-stealing backup fails | Prevent steal; coro stays on source P |

---

## 7. Performance Expectations

### 7.1 Switch Latency

| Path | Latency | memcpy | Frequency |
|------|---------|--------|-----------|
| Hot → Hot (same coro resumes) | **~15ns** | 0 | 60% (I/O bound) |
| Hot → Hot (different coro) | **~15ns** | 0 | 20% (short yield) |
| Cold → Hot (restore) | **~60ns** | 1× (2-8KB) | 15% |
| Hot → Cold (evict+restore) | **~100ns** | 2× | 5% (LRU miss) |
| Exclusive | **~15ns** | 0 | <5% (large-stack) |

### 7.2 Memory Efficiency

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Per coroutine (avg) | 64KB+ | ~4KB backup | **16x** |
| 10,000 coroutines | 640MB | ~40MB + 1MB hot | **15x** |
| 1,000,000 coroutines | 64GB | ~4GB + 1MB hot | **16x** |
| Scheduler overhead | 0 | 1MB (8 slots) | Negligible |

### 7.3 Throughput

| Benchmark | Before | After (Hot) | After (Cold) |
|-----------|--------|-------------|--------------|
| `bench_switch` | 14.48ns | 14.48ns | ~60ns |
| Channel ops/sec | 33M | 33M+ (hot) | ~25M (cold) |
| I/O latency | 714ns | 714ns (hot) | ~770ns (cold) |

---

## 8. Testing Plan

### 8.1 Correctness Tests

| Test | Description | Validation |
|------|-------------|------------|
| `test_stack_pointer` | Coro takes address of local, yields, verifies content | Pointer content intact after resume |
| `test_deep_recursion` | Fibonacci(1000) in shared stack | Result correct; watermark accurate |
| `test_large_local` | `char buf[60000]` (near 128KB limit) | No overflow; triggers exclusive fallback |
| `test_channel_stress` | 1000 coroutines, hot/cold mixed, channel comms | No data corruption |
| `test_lru_order` | Create 16 coros, run in known order, verify eviction | LRU tail is correct victim |
| `test_priority_reserved` | HIGH coro + 8 NORMAL coros; verify Slot 0 | HIGH coro never evicted |

### 8.2 Memory Tests

| Test | Tool | Validation |
|------|------|------------|
| `stress_coro_count` (100K) | ASan + Valgrind | No leaks; backup freed on exit |
| `stress_hot_cold_cycle` | ASan | No use-after-free during eviction |
| `stress_exclusive_fallback` | ASan | Exclusive stack munmap'd on destroy |

### 8.3 Performance Benchmarks

| Benchmark | Metric | Target |
|-----------|--------|--------|
| `bench_switch_hot` | Hot switch latency | < 20ns |
| `bench_switch_cold` | Cold restore latency | < 100ns |
| `bench_memory_1m` | Memory for 1M coros | < 5GB |
| `bench_lru_hit_rate` | Hit rate @ 16 active coros | > 80% |
| `bench_exclusive_trigger` | Fallback trigger latency | < 5μs |

### 8.4 Multi-Threading Tests

| Test | Description |
|------|-------------|
| `test_steal_hot_coro` | Steal hot coro, verify backup + restore |
| `test_steal_exclusive` | Steal exclusive coro, verify no crash |
| `test_mt_lru_race` | TSan verification of LRU operations |

---

## 9. Compatibility & Migration

### 9.1 API Compatibility

**100% backward compatible.** All public APIs unchanged:
- `coco_create()` — stack_size parameter ignored for shared stacks; used only for exclusive fallback
- `coco_yield()` — no API change
- `coco_exit()` — no API change
- `coco_sched_create()` — no API change

### 9.2 ABI Compatibility

Internal structure changes (`coco_coro_t`, `coco_sched_t`) are invisible to users (opaque pointers).

### 9.3 Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `COCO_HOT_SLOTS` | 8 | Number of hot stack slots per scheduler |
| `COCO_HOT_STACK_SIZE` | 131072 (128KB) | Size of each hot stack |
| `COCO_HOT_THRESHOLD` | 102400 (100KB) | Trigger exclusive fallback |

---

## 10. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Stack pointer arithmetic bugs | Critical (crash) | Extensive tests; conservative watermark calculation |
| memcpy of uninitialized stack | Medium (leak) | Only copy `stack_used` bytes; watermark accurate |
| Exclusive fallback failure | Medium (OOM) | Keep coro in hot slot; guard page catches overflow |
| LRU thrashing (worst case) | Low (perf) | 8 slots + reserved slot minimizes thrashing |
| Work-stealing backup latency | Low (perf) | Only steals cold coros when possible |

---

## 11. Implementation Phases

| Phase | Scope | Duration |
|-------|-------|----------|
| 1 | Hot slot allocation + context switch integration | 1 week |
| 2 | LRU management + yield/resume watermark | 1 week |
| 3 | Exclusive fallback + stack growth integration | 1 week |
| 4 | Multi-threading migration support | 3 days |
| 5 | Tests + benchmarks + documentation | 1 week |

**Total estimated**: 4-5 weeks

---

## Appendix: LRU Operations (O(1))

```c
/* Insert node at LRU head */
static void hot_lru_insert_head(coco_sched_t *sched, coco_hot_node_t *node) {
    node->next = sched->hot_lru_head;
    node->prev = NULL;
    if (sched->hot_lru_head) {
        sched->hot_lru_head->prev = node;
    }
    sched->hot_lru_head = node;
    if (!sched->hot_lru_tail) {
        sched->hot_lru_tail = node;
    }
}

/* Move existing node to head */
static void hot_lru_move_to_head(coco_sched_t *sched, coco_hot_node_t *node) {
    if (node == sched->hot_lru_head) return;
    
    /* Unlink */
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (node == sched->hot_lru_tail) sched->hot_lru_tail = node->prev;
    
    /* Insert at head */
    node->next = sched->hot_lru_head;
    node->prev = NULL;
    if (sched->hot_lru_head) {
        sched->hot_lru_head->prev = node;
    }
    sched->hot_lru_head = node;
}

/* Remove node from LRU */
static void hot_lru_remove(coco_sched_t *sched, coco_hot_node_t *node) {
    if (node->prev) node->prev->next = node->next;
    else sched->hot_lru_head = node->next;
    
    if (node->next) node->next->prev = node->prev;
    else sched->hot_lru_tail = node->prev;
    
    node->next = NULL;
    node->prev = NULL;
}
```
