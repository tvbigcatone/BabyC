#include <stdio.h>
int limit(void) { return 5; }
int main()
{
    int i, s;
    s = 0;
    for (i = 0; i < limit(); i = i + 1)
        s = s + i;
    printf("%d\n", s);
    return 0;
}
