/* Two guest threads: waiter drops GIL around cond_wait so signaler can run. */
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t m;
static pthread_cond_t c;
static int ready = 0;

void *waiter(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&m);
    while (ready == 0)
        pthread_cond_wait(&c, &m);
    pthread_mutex_unlock(&m);
    return 0;
}

void *signaler(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&m);
    ready = 1;
    pthread_cond_signal(&c);
    pthread_mutex_unlock(&m);
    return 0;
}

int main(void)
{
    pthread_t tw = 0;
    pthread_t ts = 0;
    void *fw = waiter;
    void *fs = signaler;
    int rc;

    rc = pthread_mutex_init(&m, 0);
    if (rc != 0) { puts("FAIL: mutex init"); return 1; }
    rc = pthread_cond_init(&c, 0);
    if (rc != 0) { puts("FAIL: cond init"); return 1; }

    rc = pthread_create(&tw, 0, fw, 0);
    if (rc != 0) { puts("FAIL: create waiter"); return 1; }
    rc = pthread_create(&ts, 0, fs, 0);
    if (rc != 0) { puts("FAIL: create signaler"); return 1; }

    rc = pthread_join(tw, 0);
    if (rc != 0) { puts("FAIL: join waiter"); return 1; }
    rc = pthread_join(ts, 0);
    if (rc != 0) { puts("FAIL: join signaler"); return 1; }

    if (ready != 1) { puts("FAIL: ready"); return 1; }
    puts("BabyPOSIX pthread cond wait/signal: ok");
    return 0;
}
