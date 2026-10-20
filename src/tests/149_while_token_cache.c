#include <stdio.h>
int main()
{
    int i, s;
    i = 0; s = 0;
    while (i < 10)
    {
        s = s + i;
        i = i + 1;
    }
    printf("%d %d\n", i, s);

    i = 0; s = 0;
    do {
        s = s + i;
        i = i + 1;
    } while (i < 5);
    printf("%d %d\n", i, s);
    return 0;
}
