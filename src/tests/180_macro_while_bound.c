#include <stdio.h>
#define LIMIT 5
int main()
{
    int i, s;
    i = 0; s = 0;
    while (i < LIMIT)
    {
        s = s + i;
        i = i + 1;
    }
    printf("%d\n", s);
    return 0;
}
