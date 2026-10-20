#include <stdio.h>
/* CROSS: ** / *p++ style via indices, array of pointers, loop */
int A[3];
int B[3];
int *P[2];

int fold(int **pp, int n)
{
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + *pp[i];
    return s;
}

int main()
{
    int i, s;
    for (i = 0; i < 3; i++)
    {
        A[i] = i + 1;
        B[i] = (i + 1) * 10;
    }
    P[0] = &A[1];
    P[1] = &B[0];
    s = fold(P, 2);
    /* mutate through pointer table */
    *P[0] = *P[0] + 100;
    s = s + A[1];
    printf("%d %d %d\n", s, A[1], *P[1]);
    return 0;
}
