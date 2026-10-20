#include <stdio.h>
/* Body IR: p++ / ++p on char* param (element step) */
int count_pp(char *p)
{
    int n = 0;
    while (*p) {
        n++;
        p++;
    }
    return n;
}
void main()
{
    printf("%d\n", count_pp("abc"));
}
