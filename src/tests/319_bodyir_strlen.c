#include <stdio.h>
/* Body IR: *p + p++ on char* param */
int my_strlen(char *p)
{
    int n = 0;
    while (*p) {
        n = n + 1;
        p = p + 1;
    }
    return n;
}
void main()
{
    printf("%d %d %d\n", my_strlen(""), my_strlen("a"), my_strlen("hello"));
}
