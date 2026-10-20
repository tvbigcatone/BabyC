#include <pthread.h>
#include <stdio.h>

static pthread_key_t key;
static long destructor_value;
static long worker_seen;

static void key_destructor(void *value)
{
    destructor_value = (long)value;
}

static void *worker(void *arg)
{
    if (pthread_setspecific(key, arg) != 0)
        return (void *)1;
    worker_seen = (long)pthread_getspecific(key);
    return pthread_getspecific(key);
}

static void *worker_exit(void *arg)
{
    if (pthread_setspecific(key, arg) != 0)
        return (void *)1;
    pthread_exit(arg);
    return 0;
}

int main(void)
{
    pthread_t thread;
    void *ret = 0;
    if (pthread_key_create(&key, key_destructor) != 0)
        return 1;
    if (pthread_setspecific(key, (void *)17) != 0)
        return 2;
    if ((long)pthread_getspecific(key) != 17)
        return 3;
    if (pthread_create(&thread, 0, worker, (void *)29) != 0)
        return 4;
    if (pthread_join(thread, &ret) != 0)
        return 5;
    if ((long)ret != 29 || worker_seen != 29 || destructor_value != 29)
        return 6;
    if ((long)pthread_getspecific(key) != 17)
        return 7;
    destructor_value = 0;
    if (pthread_create(&thread, 0, worker_exit, (void *)31) != 0)
        return 8;
    ret = 0;
    if (pthread_join(thread, &ret) != 0)
        return 9;
    if ((long)ret != 31 || destructor_value != 31)
        return 10;
    if (pthread_key_delete(key) != 0)
        return 11;
    if (pthread_getspecific(key) != 0)
        return 12;
    puts("pthread_key_tls: OK");
    return 0;
}
