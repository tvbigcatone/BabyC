#include <stdio.h>
int main()
{
    int i, n;
    i = 0; n = 0;
    do {
        n = n + 1;
        i = i + 1;
    } while (i < 3);
    printf("%d %d\n", i, n);
    return 0;
}
