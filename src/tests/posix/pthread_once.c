/* BabyPOSIX: pthread_once runs interpreted init exactly once. */
#include <stdio.h>
#include <pthread.h>

static int n;

void init(void)
{
    n = n + 7;
}

int main(void)
{
    pthread_once_t o = PTHREAD_ONCE_INIT;
    int rc;

    rc = pthread_once(&o, init);
    if (rc != 0) { printf("FAIL once rc=%d\n", rc); return 1; }
    if (n != 7) { printf("FAIL n=%d\n", n); return 1; }
    rc = pthread_once(&o, init);
    if (rc != 0) { printf("FAIL once2 rc=%d\n", rc); return 1; }
    if (n != 7) { printf("FAIL n second=%d\n", n); return 1; }
    printf("BabyPOSIX pthread once: ok\n");
    return 0;
}
