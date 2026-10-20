#include <stdio.h>
int main()
{
    int s[1000];
    int i, r;
    for (r = 0; r < 1000; r++)
        for (i = 0; i < 1000; i++)
            s[i] = 1;
    printf("%d\n", s[5]);
    return 0;
}
