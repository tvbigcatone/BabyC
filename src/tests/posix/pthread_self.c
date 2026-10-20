/* BabyPOSIX: pthread_self maps worker TLS slot; main is 0. */
#include <stdio.h>
#include <pthread.h>

static pthread_t seen;

void *worker(void *arg)
{
    (void)arg;
    seen = pthread_self();
    return 0;
}

int main(void)
{
    pthread_t th = 0;
    void *fn = worker;
    int rc;

    if (pthread_self() != 0) {
        printf("FAIL main self\n");
        return 1;
    }
    rc = pthread_create(&th, 0, fn, 0);
    if (rc != 0) { printf("FAIL create %d\n", rc); return 1; }
    rc = pthread_join(th, 0);
    if (rc != 0) { printf("FAIL join %d\n", rc); return 1; }
    if (th == 0) { printf("FAIL th=0\n"); return 1; }
    if (seen != th) {
        printf("FAIL self=%lu th=%lu\n", (unsigned long)seen, (unsigned long)th);
        return 1;
    }
    printf("BabyPOSIX pthread self: ok\n");
    return 0;
}
