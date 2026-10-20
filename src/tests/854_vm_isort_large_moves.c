#include <stdio.h>
#define N 800
/* BUG B, large variant. Expected "29665 3072177".  VM printed "30510 0". */
int main(void)
{
    int a[N];
    int i, j, key, pass;
    long long s = 0;
    int moves = 0;
    for (pass = 0; pass < 20; pass++)
    {
        for (i = 0; i < N; i++)
            a[i] = (i * 17 + pass * 3) % 997;
        for (i = 1; i < N; i++)
        {
            key = a[i];
            j = i - 1;
            while (j >= 0 && a[j] > key)
            {
                a[j + 1] = a[j];
                j--;
                moves++;
            }
            a[j + 1] = key;
        }
        s += a[0] + a[N / 2] + a[N - 1];
    }
    printf("%lld %d\n", s, moves);
    return 0;
}
