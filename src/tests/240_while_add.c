#include <stdio.h>
int main()
{
    int i;
    int s;
    i = 0;
    s = 0;
    while (i < 10) {
        s += i;
        i++;
    }
    printf("%d\n", s);
    return 0;
}
