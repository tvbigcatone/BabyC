#include <stdio.h>

int main()
{
    int i = 42;
    double f = 3.14;
    char s[] = "hello";
    int j;

    printf("int    : %d  (hex: 0x%08X)\n", i, i);
    printf("float  : %.2f  (sci: %.2e)\n", f, f);
    printf("string : %s\n", s);
    printf("char   : %c  (code: %d)\n", s[0], s[0]);
    for (j = 0; j < 4; j++)
        printf("  row %d : %*d\n", j, 6, j * j);
    return 0;
}
