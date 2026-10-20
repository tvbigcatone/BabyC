#include <stdio.h>
int main()
{
    int i;
    int s;
    i = 0;
    s = 0;
    do {
        s += i;
        i++;
    } while (i < 10);
    printf("%d\n", s);
    return 0;
}
