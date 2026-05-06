/**
 * test_channel_select.c - Channel Select 单元测试
 *
 * 测试覆盖:
 * - 有缓冲 channel 的 recv/send 就绪 select
 * - 多 channel 同时就绪 select
 * - 无缓冲 channel 的阻塞 recv/send select
 * - 超时 select
 * - default case select
 * - 已关闭 channel 的 select
 * - 混合 send/recv case 的 select
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ========== 辅助变量 ========== */

static int select_result_index;

/* ========== Test 1: select_recv_ready ========== */

static int recv_ready_value;

static void recv_ready_sender(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    static int val = 42;
    coco_channel_send(ch, &val);
}

static void select_recv_ready_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    void *val = NULL;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 0);
    select_result_index = idx;
    if (idx == 0 && val != NULL) {
        recv_ready_value = *(int *)val;
    }
}

static void test_select_recv_ready(void) {
    printf("test_select_recv_ready: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(5);

    select_result_index = -1;
    recv_ready_value = 0;
    /* Sender created first so data is in buffer before select runs */
    coco_create(sched, recv_ready_sender, ch, 0);
    coco_create(sched, select_recv_ready_coro, ch, 0);
    coco_sched_run(sched);

    assert(select_result_index == 0);
    assert(recv_ready_value == 42);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 2: select_send_ready ========== */

static int send_ready_result;

static void select_send_ready_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    static int val = 99;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_SEND;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 0);
    select_result_index = idx;
    send_ready_result = (idx == 0) ? cases[0].result : -1;
}

static void test_select_send_ready(void) {
    printf("test_select_send_ready: ");

    coco_sched_t *sched = coco_sched_create();
    /* Buffered channel with space */
    coco_channel_t *ch = coco_channel_create(5);

    select_result_index = -1;
    send_ready_result = -1;
    coco_create(sched, select_send_ready_coro, ch, 0);
    coco_sched_run(sched);

    assert(select_result_index == 0);
    assert(send_ready_result == COCO_OK);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 3: select_multi_ready ========== */

static int multi_ready_value;

static void multi_ready_sender1(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    static int val = 10;
    coco_channel_send(ch, &val);
}

static void multi_ready_sender2(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    static int val = 20;
    coco_channel_send(ch, &val);
}

static void select_multi_ready_coro(void *arg) {
    coco_channel_t **chs = (coco_channel_t **)arg;

    void *val = NULL;
    coco_select_case_t cases[2];
    cases[0].chan = chs[0];
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;
    cases[1].chan = chs[1];
    cases[1].dir = COCO_SELECT_RECV;
    cases[1].val = &val;

    int idx = coco_channel_select(cases, 2, 0, 0);
    select_result_index = idx;
    if (idx >= 0 && val != NULL) {
        multi_ready_value = *(int *)val;
    }
}

static void test_select_multi_ready(void) {
    printf("test_select_multi_ready: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch1 = coco_channel_create(5);
    coco_channel_t *ch2 = coco_channel_create(5);

    static coco_channel_t *chs[2];
    chs[0] = ch1;
    chs[1] = ch2;

    select_result_index = -1;
    multi_ready_value = 0;
    /* Senders first so both channels have data before select runs */
    coco_create(sched, multi_ready_sender1, ch1, 0);
    coco_create(sched, multi_ready_sender2, ch2, 0);
    coco_create(sched, select_multi_ready_coro, chs, 0);
    coco_sched_run(sched);

    /* First-ready scan: ch1 should be selected since it's checked first */
    assert(select_result_index == 0);
    assert(multi_ready_value == 10);

    coco_channel_destroy(ch1);
    coco_channel_destroy(ch2);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 4: select_recv_block ========== */

static volatile int recv_block_done;
static int recv_block_value;

static void select_recv_block_sender(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    /* Let the receiver block first, then send */
    coco_yield();
    static int val = 77;
    coco_channel_send(ch, &val);
}

static void select_recv_block_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    void *val = NULL;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 0);
    if (idx == 0 && val != NULL) {
        recv_block_value = *(int *)val;
        recv_block_done = 1;
    }
}

static void test_select_recv_block(void) {
    printf("test_select_recv_block: ");

    coco_sched_t *sched = coco_sched_create();
    /* Unbuffered channel */
    coco_channel_t *ch = coco_channel_create(0);

    recv_block_done = 0;
    recv_block_value = 0;

    /* Receiver first — will block; sender yields then sends */
    coco_create(sched, select_recv_block_coro, ch, 0);
    coco_create(sched, select_recv_block_sender, ch, 0);
    coco_sched_run(sched);

    assert(recv_block_done == 1);
    assert(recv_block_value == 77);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 5: select_send_block ========== */

static volatile int send_block_done;

static void select_send_block_receiver(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    /* Let the sender block first, then receive */
    coco_yield();
    void *val = NULL;
    coco_channel_recv(ch, &val);
}

static void select_send_block_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    static int val = 55;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_SEND;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 0);
    if (idx == 0) {
        send_block_done = 1;
    }
}

static void test_select_send_block(void) {
    printf("test_select_send_block: ");

    coco_sched_t *sched = coco_sched_create();
    /* Unbuffered channel — send will block until a receiver arrives */
    coco_channel_t *ch = coco_channel_create(0);

    send_block_done = 0;

    /* Sender first — will block; receiver yields then receives */
    coco_create(sched, select_send_block_coro, ch, 0);
    coco_create(sched, select_send_block_receiver, ch, 0);
    coco_sched_run(sched);

    assert(send_block_done == 1);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 6: select_timeout ========== */

static int timeout_result;

static void select_timeout_coro(void *arg) {
    (void)arg;

    /* Use an unbuffered channel with no peer — select will block */
    coco_channel_t *ch = coco_channel_create(0);

    void *val = NULL;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 50, 0);
    timeout_result = idx;

    coco_channel_destroy(ch);
}

static void test_select_timeout(void) {
    printf("test_select_timeout: ");

    coco_sched_t *sched = coco_sched_create();

    timeout_result = 0;
    coco_create(sched, select_timeout_coro, NULL, 0);
    coco_sched_run(sched);

    assert(timeout_result == COCO_SELECT_TIMEOUT);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 7: select_default ========== */

static int default_result;

static void select_default_coro(void *arg) {
    (void)arg;

    /* Unbuffered channel, no peer — no case ready, default should fire */
    coco_channel_t *ch = coco_channel_create(0);

    void *val = NULL;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 1);
    default_result = idx;

    coco_channel_destroy(ch);
}

static void test_select_default(void) {
    printf("test_select_default: ");

    coco_sched_t *sched = coco_sched_create();

    default_result = 0;
    coco_create(sched, select_default_coro, NULL, 0);
    coco_sched_run(sched);

    assert(default_result == COCO_SELECT_DEFAULT);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 8: select_default_with_ready ========== */

static int default_with_ready_idx;
static int default_with_ready_value;

static void default_with_ready_sender(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    static int val = 33;
    coco_channel_send(ch, &val);
}

static void select_default_with_ready_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    void *val = NULL;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 1);
    default_with_ready_idx = idx;
    if (idx == 0 && val != NULL) {
        default_with_ready_value = *(int *)val;
    }
}

static void test_select_default_with_ready(void) {
    printf("test_select_default_with_ready: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(5);

    default_with_ready_idx = -99;
    default_with_ready_value = 0;
    /* Sender first so data is in buffer before select runs */
    coco_create(sched, default_with_ready_sender, ch, 0);
    coco_create(sched, select_default_with_ready_coro, ch, 0);
    coco_sched_run(sched);

    assert(default_with_ready_idx == 0);
    assert(default_with_ready_value == 33);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 9: select_closed_channel ========== */

static int closed_send_result;
static int closed_send_case_result;

static void closed_channel_sender_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    static int val = 1;
    coco_select_case_t cases[1];
    cases[0].chan = ch;
    cases[0].dir = COCO_SELECT_SEND;
    cases[0].val = &val;

    int idx = coco_channel_select(cases, 1, 0, 0);
    closed_send_result = idx;
    closed_send_case_result = cases[0].result;
}

static void test_select_closed_channel(void) {
    printf("test_select_closed_channel: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(0);
    coco_channel_close(ch);

    closed_send_result = -99;
    closed_send_case_result = 0;
    coco_create(sched, closed_channel_sender_coro, ch, 0);
    coco_sched_run(sched);

    /* Send on closed channel: select should return the case index with CLOSED error */
    assert(closed_send_result == 0);
    assert(closed_send_case_result == COCO_ERROR_CHANNEL_CLOSED);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Test 10: select_mixed_send_recv ========== */

static volatile int mixed_recv_done;
static int mixed_recv_value;

static void mixed_preloader(void *arg) {
    coco_channel_t **chs = (coco_channel_t **)arg;
    /* Load data into chs[1] (buffered channel) */
    static int val = 66;
    coco_channel_send(chs[1], &val);
}

static void select_mixed_coro(void *arg) {
    coco_channel_t **chs = (coco_channel_t **)arg;

    /* chs[0]: unbuffered, try to send; chs[1]: will have data, try to recv */
    static int send_val = 99;
    void *recv_val = NULL;

    coco_select_case_t cases[2];
    cases[0].chan = chs[0];
    cases[0].dir = COCO_SELECT_SEND;
    cases[0].val = &send_val;
    cases[1].chan = chs[1];
    cases[1].dir = COCO_SELECT_RECV;
    cases[1].val = &recv_val;

    int idx = coco_channel_select(cases, 2, 0, 0);
    if (idx == 1 && recv_val != NULL) {
        mixed_recv_value = *(int *)recv_val;
        mixed_recv_done = 1;
    }
}

static void test_select_mixed_send_recv(void) {
    printf("test_select_mixed_send_recv: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch1 = coco_channel_create(0);  /* unbuffered, for send */
    coco_channel_t *ch2 = coco_channel_create(5);  /* buffered, pre-loaded for recv */

    static coco_channel_t *chs[2];
    chs[0] = ch1;
    chs[1] = ch2;

    mixed_recv_done = 0;
    mixed_recv_value = 0;

    /* Preloader first so ch2 has data before select runs */
    coco_create(sched, mixed_preloader, chs, 0);
    coco_create(sched, select_mixed_coro, chs, 0);
    coco_sched_run(sched);

    /* ch2 has data, so recv case (index 1) should be selected */
    assert(mixed_recv_done == 1);
    assert(mixed_recv_value == 66);

    coco_channel_destroy(ch1);
    coco_channel_destroy(ch2);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== Main ========== */

int main(void) {
    printf("=== Channel Select Tests ===\n");

    test_select_recv_ready();
    test_select_send_ready();
    test_select_multi_ready();
    test_select_recv_block();
    test_select_send_block();
    test_select_timeout();
    test_select_default();
    test_select_default_with_ready();
    test_select_closed_channel();
    test_select_mixed_send_recv();

    printf("All tests passed!\n");
    return 0;
}
