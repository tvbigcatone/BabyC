/* Fase C: global array/scalar bind — loop uses CachedVal, no per-iter TableGet.
 * Correctness: sum/write on global array and scalar.
 */
#include <stdio.h>
int G[8];
int S;
int main(void)
{
    int i, sum = 0;
    for (i = 0; i < 8; i++)
        G[i] = i * 3;
    for (i = 0; i < 8; i++)
        sum += G[i];
    S = sum;
    printf("%d %d %d\n", G[0], G[7], S);
    G[3] = 99;
    printf("%d\n", G[3]);
    return 0;
}
