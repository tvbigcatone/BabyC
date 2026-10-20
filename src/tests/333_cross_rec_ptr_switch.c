/* Cross: recursion + int* params + arrays + switch — hanoi-class regression net */
#include <stdio.h>

#define N 3
int A[N], B[N], C[N];

void dump(int *t)
{
    int i;
    for (i = 0; i < N; i++)
        printf("%d", t[i]);
}

int move(int *src, int *dst)
{
    int i = 0, j = 0;
    while (i < N && src[i] == 0) i++;
    while (j < N && dst[j] == 0) j++;
    dst[j - 1] = src[i];
    src[i] = 0;
    return 1;
}

/* recurse with permuted pointers; switch chooses path by n */
void h(int n, int *s, int *d, int *p)
{
    switch (n) {
        case 1:
            move(s, d);
            return;
        default:
            h(n - 1, s, p, d);
            move(s, d);
            h(n - 1, p, d, s);
            return;
    }
}

int main()
{
    int i;
    for (i = 0; i < N; i++) { A[i] = i + 1; B[i] = 0; C[i] = 0; }
    h(N, A, B, C);
    dump(A); printf(" ");
    dump(B); printf(" ");
    dump(C); printf("\n");
    return 0;
}
