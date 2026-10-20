#include <stdio.h>
#define N 4
int A[N][N];
int B[N][N];
int C[N][N];

void main()
{
    int i, j, k, sum;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            A[i][j] = i + 1;
            B[i][j] = j + 1;
        }
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            sum = 0;
            for (k = 0; k < N; k++)
                sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }
    printf("c00=%d c03=%d c30=%d c33=%d\n", C[0][0], C[0][3], C[3][0], C[3][3]);
}
