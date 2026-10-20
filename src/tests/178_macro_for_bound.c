#include <stdio.h>
#define N 4
int main()
{
    int i, s;
    s = 0;
    for (i = 0; i < N; i = i + 1)
        s = s + 1;
    printf("%d\n", s);
    return 0;
}
