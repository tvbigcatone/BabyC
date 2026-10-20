#include <stdio.h>
#define N 40
int main() {
    int A[N][N], B[N][N], C[N][N];
    int i, j, k;
    long long s = 0;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            A[i][j] = (i + j) % 17;
            B[i][j] = (i * j + 1) % 13;
            C[i][j] = 0;
        }
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            for (k = 0; k < N; k++)
                C[i][j] += A[i][k] * B[k][j];
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            s += C[i][j];
    printf("%lld\n", s);
    return 0;
}
