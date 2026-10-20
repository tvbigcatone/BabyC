/* BabyPOSIX: detached threads recycle table slots after they finish. */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

static int done_count;

void *worker(void *arg)
{
    (void)arg;
    done_count = done_count + 1;
    return 0;
}

int main(void)
{
    pthread_attr_t at;
    void *fn = worker;
    int i;
    int rc;
    pthread_t th;

    rc = pthread_attr_init(&at);
    if (rc != 0) { printf("FAIL attr init %d\n", rc); return 1; }
    rc = pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (rc != 0) { printf("FAIL setdetach %d\n", rc); return 1; }

    for (i = 0; i < 80; i++) {
        th = 0;
        rc = pthread_create(&th, &at, fn, 0);
        if (rc != 0)
            break;
    }
    if (i < 60) {
        printf("FAIL create exhausted too early i=%d rc=%d\n", i, rc);
        return 1;
    }

    for (i = 0; i < 200 && done_count < 60; i++)
        usleep(1000);
    if (done_count < 60) {
        printf("FAIL workers not finished done=%d\n", done_count);
        return 1;
    }

    th = 0;
    rc = pthread_create(&th, &at, fn, 0);
    if (rc != 0) {
        printf("FAIL slot not recycled rc=%d EAGAIN=%d\n", rc, EAGAIN);
        return 1;
    }
    for (i = 0; i < 50; i++)
        usleep(1000);
    pthread_attr_destroy(&at);
    printf("BabyPOSIX pthread detached recycle: ok\n");
    return 0;
}
