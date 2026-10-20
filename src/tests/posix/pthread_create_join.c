/* BabyPOSIX: pthread_create + join with interpreted start_routine (GIL trampoline). */
#include <stdio.h>
#include <pthread.h>

static int g_flag = 0;

void *worker(void *arg)
{
    int *p = (int *)arg;
    if (p != 0)
        *p = *p + 1;
    g_flag = 42;
    return 0;
}

int main(void)
{
    pthread_t th;
    int cell = 7;
    int rc;
    void *ret = 0;
    /* Function designator → void* is supported (ExpressionAssignToPointer).
     * IR currently residualizes calls that pass bare function names; tree
     * path (and BABYC_NO_IR=1) always exercises the trampoline. */
    void *start = worker;

    rc = pthread_create(&th, 0, start, &cell);
    if (rc != 0) {
        printf("FAIL create %d\n", rc);
        return 1;
    }
    rc = pthread_join(th, &ret);
    if (rc != 0) {
        printf("FAIL join %d\n", rc);
        return 1;
    }
    if (g_flag != 42) {
        printf("FAIL g_flag=%d\n", g_flag);
        return 1;
    }
    if (cell != 8) {
        printf("FAIL cell=%d\n", cell);
        return 1;
    }
    printf("BabyPOSIX pthread create join: ok\n");
    return 0;
}
