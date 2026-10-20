#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

static int got_type;

int main(void)
{
    pthread_mutexattr_t attr;
    pthread_mutex_t recursive = 0;
    pthread_mutex_t mutex = 0;
    pthread_cond_t cond = 0;
    struct timespec deadline;
    int rc;
    if (pthread_mutexattr_init(&attr) != 0) return 1;
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) return 2;
    rc = pthread_mutexattr_gettype(&attr, &got_type);
    if (rc != 0) return 3;
    if (got_type != 1) { printf("gettype type=%d expected=1\n", got_type); return 3; }
    if (pthread_mutex_init(&recursive, &attr) != 0) return 4;
    if (pthread_mutex_lock(&recursive) != 0 || pthread_mutex_lock(&recursive) != 0) return 5;
    if (pthread_mutex_unlock(&recursive) != 0 || pthread_mutex_unlock(&recursive) != 0) return 6;
    if (pthread_mutex_destroy(&recursive) != 0 || pthread_mutexattr_destroy(&attr) != 0) return 7;
    if (pthread_mutex_init(&mutex, 0) != 0 || pthread_cond_init(&cond, 0) != 0) return 8;
    deadline.tv_sec = 1;
    deadline.tv_nsec = 0;
    if (pthread_mutex_lock(&mutex) != 0) return 9;
    rc = pthread_cond_timedwait(&cond, &mutex, &deadline);
    if (pthread_mutex_unlock(&mutex) != 0) return 10;
    if (rc != ETIMEDOUT) { puts("pthread_cond_timedwait timeout contract"); return 11; }
    if (pthread_cond_destroy(&cond) != 0 || pthread_mutex_destroy(&mutex) != 0) return 12;
    puts("BabyPOSIX pthread attributes/timedwait: ok");
    return 0;
}
