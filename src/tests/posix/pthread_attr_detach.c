/* BabyPOSIX: pthread_create honors PTHREAD_CREATE_DETACHED. */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

static int done;

void *worker(void *arg)
{
    (void)arg;
    done = 1;
    return 0;
}

int main(void)
{
    pthread_t th = 0;
    pthread_attr_t at;
    void *fn = worker;
    int rc;
    int spins;

    rc = pthread_attr_init(&at);
    if (rc != 0) { printf("FAIL attr init %d\n", rc); return 1; }
    rc = pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (rc != 0) { printf("FAIL setdetach %d\n", rc); return 1; }
    rc = pthread_create(&th, &at, fn, 0);
    if (rc != 0) { printf("FAIL create %d\n", rc); return 1; }
    rc = pthread_join(th, 0);
    if (rc != EINVAL) {
        printf("FAIL join detached expected EINVAL got %d\n", rc);
        return 1;
    }
    for (spins = 0; spins < 50 && done == 0; spins++)
        usleep(1000);
    if (done != 1) { printf("FAIL worker not done\n"); return 1; }
    pthread_attr_destroy(&at);
    printf("BabyPOSIX pthread attr detach: ok\n");
    return 0;
}
