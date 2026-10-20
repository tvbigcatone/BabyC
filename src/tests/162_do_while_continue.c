#include <stdio.h>
int main()
{
    int i, s;
    i = 0; s = 0;
    do {
        i = i + 1;
        if (i == 3)
            continue;
        s = s + i;
    } while (i < 5);
    printf("%d %d\n", i, s);
    return 0;
}
