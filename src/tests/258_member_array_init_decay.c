#include <stdio.h>
#include <string.h>
struct S { char name[8]; int a[3]; int n; };
void fill(int *p)
{
    p[0] = 1;
    p[1] = 2;
    p[2] = 3;
}
int main()
{
    struct S s;
    strcpy(s.name, "ab");
    fill(s.a);
    s.n = 3;
    printf("%s %c %d %d %d %d\n", s.name, s.name[1], s.a[0], s.a[1], s.a[2], s.n);
    return 0;
}
