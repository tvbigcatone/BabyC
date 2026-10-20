#include <stdio.h>
int main()
{
    int i;
    i = 0;
    do {
        if (i >= 2)
            break;
        printf("%d\n", i);
        i = i + 1;
    } while (1);
    printf("ok %d\n", i);
    return 0;
}
