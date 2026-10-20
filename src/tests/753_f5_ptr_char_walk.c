#include <stdio.h>
/* char* walk with ++ and *p */
int main(void)
{
    char buf[6];
    char *p;
    int n = 0;
    buf[0]='a'; buf[1]='b'; buf[2]='c'; buf[3]='d'; buf[4]='e'; buf[5]=0;
    for (p = buf; *p; p++)
        n += (int)*p;
    printf("%d\n", n);
    return 0;
}
