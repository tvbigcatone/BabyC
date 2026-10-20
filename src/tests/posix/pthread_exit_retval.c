/* BabyPOSIX: pthread_exit stores the value join observes. */
#include <stdio.h>
#include <pthread.h>

static int cell = 7;
static void *joined;

void *worker(void *arg)
{
    (void)arg;
    pthread_exit(&cell);
    return 0;
}

int main(void)
{
    pthread_t th = 0;
    void *fn = worker;
    int rc;

    joined = 0;
    rc = pthread_create(&th, 0, fn, 0);
    if (rc != 0) { printf("FAIL create %d\n", rc); return 1; }
    rc = pthread_join(th, &joined);
    if (rc != 0) { printf("FAIL join %d\n", rc); return 1; }
    if (joined != &cell) {
        printf("FAIL joined=%p expected %p\n", joined, (void *)&cell);
        return 1;
    }
    printf("BabyPOSIX pthread exit retval: ok\n");
    return 0;
}
