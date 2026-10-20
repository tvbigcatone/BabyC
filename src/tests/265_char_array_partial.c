#include <stdio.h>
int main()
{
    char b[4];
    b[0] = 'A';
    b[1] = 'B';
    b[2] = 0;
    printf("%c%c%d\n", b[0], b[1], b[2]);
    printf("%c\n", b[3]);
    return 0;
}
