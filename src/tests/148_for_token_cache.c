/* Token-cached for: multi-iteration with side effects in body */
#include <stdio.h>
int main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 10; i = i + 1)
        s = s + i;
    printf("%d %d\n", i, s);

    /* nested for */
    s = 0;
    for (i = 0; i < 3; i = i + 1)
    {
        int j;
        for (j = 0; j < 3; j = j + 1)
            s = s + 1;
    }
    printf("%d\n", s);

    /* break */
    s = 0;
    for (i = 0; i < 100; i = i + 1)
    {
        if (i == 5)
            break;
        s = s + 1;
    }
    printf("%d %d\n", i, s);

    /* continue */
    s = 0;
    for (i = 0; i < 10; i = i + 1)
    {
        if (i == 3)
            continue;
        s = s + i;
    }
    printf("%d\n", s);
    return 0;
}
