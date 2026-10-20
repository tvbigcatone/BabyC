/* BabyPOSIX: destroy of unused INITIALIZER is a no-op (rc 0). */
#include <stdio.h>
#include <pthread.h>

int main(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    int rc;

    rc = pthread_mutex_destroy(&m);
    if (rc != 0) { printf("FAIL mutex destroy unused %d\n", rc); return 1; }
    rc = pthread_cond_destroy(&c);
    if (rc != 0) { printf("FAIL cond destroy unused %d\n", rc); return 1; }
    printf("BabyPOSIX pthread destroy initializer: ok\n");
    return 0;
}
