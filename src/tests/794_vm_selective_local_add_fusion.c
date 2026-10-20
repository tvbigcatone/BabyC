#include <stdio.h>

static int sum_to(int n)
{
    int i = 0;
    int sum = 0;
    while (i < n) {
        i = i + 1;
        sum = sum + i;
    }
    return sum;
}

static long long sum_wide(int n)
{
    int i = 0;
    long long sum = 0;
    while (i < n) {
        i = i + 1;
        sum = sum + i;
    }
    return sum;
}

static unsigned int count_up(unsigned int n)
{
    unsigned int i = 0;
    while (i < n) {
        i = i + 1;
    }
    return i;
}

int main(void)
{
    printf("%d %u %lld %d\n", sum_to(100), count_up(19U), sum_wide(100), sum_to(0));
    return 0;
}
