/* BabyPOSIX: null start_routine must fail cleanly (EINVAL or ENOSYS). */
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

int main(void)
{
    pthread_t th;
    int rc = pthread_create(&th, 0, 0, 0);
    if (rc != EINVAL && rc != ENOSYS) {
        printf("FAIL expected EINVAL/ENOSYS got %d\n", rc);
        return 1;
    }
    printf("BabyPOSIX pthread create null: ok\n");
    return 0;
}
