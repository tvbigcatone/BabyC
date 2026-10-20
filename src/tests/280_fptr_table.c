#include <stdio.h>

int f0(int x) { return x; }
int f1(int x) { return x + 1; }
int f2(int x) { return x + 2; }

struct Entry {
    int (*fn)(int);
};

int main()
{
    struct Entry tab[3];
    int s;

    tab[0].fn = &f0;
    tab[1].fn = &f1;
    tab[2].fn = &f2;

    /* unrolled: array-of-fptr-decl unsupported; call via member */
    s = (*tab[0].fn)(10) + (*tab[1].fn)(10) + (*tab[2].fn)(10);
    printf("s %d\n", s);
    printf("one %d\n", (*tab[1].fn)(5));
    printf("ok\n");
    return 0;
}
