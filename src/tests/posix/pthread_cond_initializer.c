/* BabyPOSIX: PTHREAD_COND_INITIALIZER lazy-allocates like mutex. */
#include <stdio.h>
#include <pthread.h>

int main(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    int rc;

    rc = pthread_mutex_lock(&m);
    if (rc != 0) { printf("FAIL lock %d\n", rc); return 1; }
    rc = pthread_cond_signal(&c);
    if (rc != 0) { printf("FAIL signal %d\n", rc); return 1; }
    rc = pthread_cond_broadcast(&c);
    if (rc != 0) { printf("FAIL broadcast %d\n", rc); return 1; }
    rc = pthread_mutex_unlock(&m);
    if (rc != 0) { printf("FAIL unlock %d\n", rc); return 1; }
    rc = pthread_cond_destroy(&c);
    if (rc != 0) { printf("FAIL destroy %d\n", rc); return 1; }
    rc = pthread_mutex_destroy(&m);
    if (rc != 0) { printf("FAIL mutex destroy %d\n", rc); return 1; }
    printf("BabyPOSIX pthread cond initializer: ok\n");
    return 0;
}
