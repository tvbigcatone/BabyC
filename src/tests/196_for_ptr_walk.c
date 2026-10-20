#include <stdio.h>
int main()
{
    char s[6];
    char *p;
    int n;
    s[0]='a'; s[1]='b'; s[2]='c'; s[3]='d'; s[4]='e'; s[5]=0;
    n = 0;
    for (p = s; *p != 0; p = p + 1)
        n = n + 1;
    printf("%d\n", n);
    return 0;
}
