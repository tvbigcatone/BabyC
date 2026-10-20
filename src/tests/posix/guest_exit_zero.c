/* BabyPOSIX: exit(0) in an interpreted worker returns through the guest
 * trampoline without corrupting interpreter stack state or the main thread. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static int after_exit;

void *worker(void *arg)
{
    (void)arg;
    exit(0);
    after_exit = 1;
    return 0;
}

int main(void)
{
    pthread_t th = 0;
    void *fn = worker;
    void *ret = (void *)1;
    int rc = pthread_create(&th, 0, fn, 0);
    if (rc != 0) { printf("FAIL create %d\n", rc); return 1; }
    rc = pthread_join(th, &ret);
    if (rc != 0) { printf("FAIL join %d\n", rc); return 1; }
    if (after_exit != 0) { printf("FAIL worker continued\n"); return 1; }
    if (ret != 0) { printf("FAIL retval %p\n", ret); return 1; }
    printf("BabyPOSIX guest worker exit zero: ok\n");
    return 0;
}
