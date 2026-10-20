#include <stdio.h>
int main()
{
    int i = 0, n = 0;
    while (i < 10) {
        i = i + 1;
        if (i % 2 == 0)
            continue;
        n = n + 1;
    }
    printf("%d %d\n", i, n);
    return 0;
}
