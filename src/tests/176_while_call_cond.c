#include <stdio.h>
int lim(void) { return 3; }
int main()
{
    int i;
    i = 0;
    while (i < lim())
    {
        printf("%d\n", i);
        i = i + 1;
    }
    printf("after\n");
    return 0;
}
