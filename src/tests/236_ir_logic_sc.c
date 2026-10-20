#include <stdio.h>
int hits;
int mark(int v)
{
    hits = hits + 1;
    return v;
}
int main()
{
    hits = 0;
    if (0 && mark(1))
        printf("no\n");
    printf("h1=%d\n", hits);
    hits = 0;
    if (1 || mark(1))
        printf("yes\n");
    printf("h2=%d\n", hits);
    hits = 0;
    if (1 && mark(2))
        printf("ok\n");
    printf("h3=%d\n", hits);
    return 0;
}
