#include <stdio.h>
int visits;
int oddstep(int *p, int n);
int evenstep(int *p, int n)
{
    visits++;
    if (n == 0) { *p += 1; return *p; }
    int child;
    *p += n;
    child = oddstep(p, n - 1);
    return child + *p;
}
int oddstep(int *p, int n)
{
    visits++;
    if (n == 0) { *p += 2; return *p; }
    int child;
    *p += 2 * n;
    child = evenstep(p, n - 1);
    return child - *p;
}
int main(void)
{
    int x = 0, r;
    visits = 0;
    r = evenstep(&x, 5);
    printf("%d %d %d\n", r, x, visits);
    return 0;
}
