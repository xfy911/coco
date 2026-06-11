#include "coco.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static int g_destroyed = 0;

static void test_destructor(void *value) {
    g_destroyed++;
    free(value);
}

static void cls_test_coro(void *arg) {
    (void)arg;
    
    int *value = malloc(sizeof(int));
    *value = 42;
    
    int ret = coco_cls_set("test_key", value, test_destructor);
    assert(ret == COCO_OK);
    
    void *retrieved = coco_cls_get("test_key");
    assert(retrieved != NULL);
    assert(*(int *)retrieved == 42);
    
    ret = coco_cls_delete("test_key");
    assert(ret == COCO_OK);
    assert(g_destroyed == 1);
    
    /* Test overwrite */
    int *value2 = malloc(sizeof(int));
    *value2 = 100;
    g_destroyed = 0;
    
    ret = coco_cls_set("test_key2", value2, test_destructor);
    assert(ret == COCO_OK);
    
    int *value3 = malloc(sizeof(int));
    *value3 = 200;
    ret = coco_cls_set("test_key2", value3, test_destructor);
    assert(ret == COCO_OK);
    assert(g_destroyed == 1); /* Old value destroyed */
    
    /* Cleanup by coroutine exit */
    g_destroyed = 0;
}

static void cls_cleanup_test(void *arg) {
    (void)arg;
    
    int *value = malloc(sizeof(int));
    *value = 999;
    
    int ret = coco_cls_set("auto_cleanup", value, test_destructor);
    assert(ret == COCO_OK);
    
    /* Exit without deleting - destructor should be called */
    g_destroyed = 0;
}

int main(void) {
    /* Test 1: Basic CLS operations */
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);
        
        g_destroyed = 0;
        coco_create(sched, cls_test_coro, NULL, 0);
        coco_sched_run(sched);
        
        coco_sched_destroy(sched);
    }
    
    /* Test 2: Automatic cleanup on coroutine exit */
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);
        
        g_destroyed = 0;
        coco_create(sched, cls_cleanup_test, NULL, 0);
        coco_sched_run(sched);
        
        assert(g_destroyed == 1); /* Destructor called on exit */
        
        coco_sched_destroy(sched);
    }
    
    /* Test 3: Outside coroutine should fail gracefully */
    {
        void *ret = coco_cls_get("any_key");
        assert(ret == NULL);
    }
    
    printf("All CLS tests passed\n");
    return 0;
}
