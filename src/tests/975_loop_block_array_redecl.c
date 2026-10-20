#include <stdio.h>
/* A block-scope array declared inside a loop reuses the same storage on
 * every iteration: the virtual stack charge must not accumulate (was a
 * false "stack overflow" after ~500 iterations on the tree path). */
static long sum_rows(int n)
{
    long s = 0;
    int k;
    for (k = 0; k < n; k++) {
        int big[1000];
        int j;
        for (j = 0; j < 4; j++)
            big[j] = k + j;
        s += big[k % 4];
    }
    return s;
}
int main(void)
{
    long s = 0;
    int k;
    for (k = 0; k < 20000; k++) {
        char buf[600];
        buf[k % 600] = (char)(k & 63);
        s += buf[k % 600];
    }
    printf("%ld\n", s);
    printf("%ld\n", sum_rows(5000));
    return 0;
}
