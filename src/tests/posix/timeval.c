/* BabyPOSIX smoke test: intended to run through the BabyC interpreter. */
#include <sys/time.h>
#include <stdio.h>

int main(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) {
        puts("gettimeofday failed");
        return 1;
    }
    if (tv.tv_sec <= 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000) {
        puts("invalid timeval");
        return 2;
    }
    puts("BabyPOSIX timeval: ok");
    return 0;
}
