/* do-while with literal bound */
#include <stdio.h>
int main()
{
    int i, s;
    i = 0; s = 0;
    do {
        s = s + i;
        i = i + 1;
    } while (i < 10);
    printf("%d %d\n", i, s);
    return 0;
}
