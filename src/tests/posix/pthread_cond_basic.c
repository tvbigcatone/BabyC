/* BabyPOSIX: cond init/signal/broadcast/destroy (single-threaded smoke). */
#include <stdio.h>
#include <pthread.h>

int main(void)
{
    pthread_mutex_t m;
    pthread_cond_t c;
    int rc;

    rc = pthread_mutex_init(&m, 0);
    if (rc != 0) { printf("FAIL mutex init %d\n", rc); return 1; }
    rc = pthread_cond_init(&c, 0);
    if (rc != 0) { printf("FAIL cond init %d\n", rc); return 1; }

    /* signal/broadcast with no waiters must succeed */
    rc = pthread_cond_signal(&c);
    if (rc != 0) { printf("FAIL signal %d\n", rc); return 1; }
    rc = pthread_cond_broadcast(&c);
    if (rc != 0) { printf("FAIL broadcast %d\n", rc); return 1; }

    rc = pthread_cond_destroy(&c);
    if (rc != 0) { printf("FAIL cond destroy %d\n", rc); return 1; }
    rc = pthread_mutex_destroy(&m);
    if (rc != 0) { printf("FAIL mutex destroy %d\n", rc); return 1; }

    printf("BabyPOSIX pthread cond: ok\n");
    return 0;
}
