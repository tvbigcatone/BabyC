/* Sieve of Eratosthenes — count primes < N */
#include <stdio.h>
#define N 200
int mark[200];
int main()
{
    int i, j, count;
    count = 0;
    for (i = 0; i < N; i++)
        mark[i] = 0;
    for (i = 2; i < N; i++)
    {
        if (mark[i] == 0)
        {
            count++;
            for (j = i + i; j < N; j = j + i)
                mark[j] = 1;
        }
    }
    printf("%d\n", count);
    return 0;
}
