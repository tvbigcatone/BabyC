#include <stdio.h>
int main()
{
    char buf[8];
    char *p;
    int i;
    p = buf;
    for (i = 0; i < 5; i = i + 1)
        *(p + i) = 'A' + i;
    *(p + 5) = 0;
    printf("%s\n", buf);
    printf("%c\n", *(p + 2));
    return 0;
}
