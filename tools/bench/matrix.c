#include <stdio.h>
int main()
{
    int m[40][40];
    int i, j, k, t;
    t = 0;
    for (k = 0; k < 10; k++) {
        for (i = 0; i < 40; i++)
            for (j = 0; j < 40; j++)
                m[i][j] = i + j + k;
        for (i = 0; i < 40; i++)
            for (j = 0; j < 40; j++)
                t += m[i][j];
    }
    printf("%d\n", t);
    return 0;
}
