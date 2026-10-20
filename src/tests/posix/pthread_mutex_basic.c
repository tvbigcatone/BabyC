/* BabyPOSIX: mutex init/lock/trylock/unlock/destroy + static INITIALIZER. */
#include <stdio.h>
#include <pthread.h>

int main(void)
{
    pthread_mutex_t m;
    pthread_mutex_t static_m = PTHREAD_MUTEX_INITIALIZER;
    int rc;

    rc = pthread_mutex_init(&m, 0);
    if (rc != 0) { printf("FAIL init %d\n", rc); return 1; }

    rc = pthread_mutex_lock(&m);
    if (rc != 0) { printf("FAIL lock %d\n", rc); return 1; }

    rc = pthread_mutex_trylock(&m);
    if (rc == 0) { printf("FAIL trylock expected busy\n"); return 1; }

    rc = pthread_mutex_unlock(&m);
    if (rc != 0) { printf("FAIL unlock %d\n", rc); return 1; }

    rc = pthread_mutex_trylock(&m);
    if (rc != 0) { printf("FAIL trylock free %d\n", rc); return 1; }
    pthread_mutex_unlock(&m);

    rc = pthread_mutex_destroy(&m);
    if (rc != 0) { printf("FAIL destroy %d\n", rc); return 1; }

    /* Static initializer: first lock allocates. */
    rc = pthread_mutex_lock(&static_m);
    if (rc != 0) { printf("FAIL static lock %d\n", rc); return 1; }
    rc = pthread_mutex_unlock(&static_m);
    if (rc != 0) { printf("FAIL static unlock %d\n", rc); return 1; }
    rc = pthread_mutex_destroy(&static_m);
    if (rc != 0) { printf("FAIL static destroy %d\n", rc); return 1; }

    printf("BabyPOSIX pthread mutex: ok\n");
    return 0;
}
