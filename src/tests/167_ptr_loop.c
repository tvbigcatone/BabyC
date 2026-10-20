#include <stdio.h>
int main()
{
    char s[6];
    char *p;
    int n;
    s[0] = 'h'; s[1] = 'i'; s[2] = 0;
    n = 0;
    for (p = s; *p != 0; p = p + 1)
        n = n + 1;
    printf("%d %c\n", n, s[0]);
    return 0;
}
