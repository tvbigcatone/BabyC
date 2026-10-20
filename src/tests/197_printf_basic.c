#include <stdio.h>
int main()
{
    int a = 42;
    unsigned u = 255;
    char c = 'Z';
    char *s = "hi";
    printf("%d\n", a);
    printf("%u\n", u);
    printf("%x %X\n", u, u);
    printf("%c\n", c);
    printf("%s\n", s);
    printf("%%\n");
    printf("a=%d s=%s\n", a, s);
    return 0;
}
