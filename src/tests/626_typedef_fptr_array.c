/* Typedef name + array of fptr must compile as Body-IR (not token fallback). */
#include <stdio.h>

typedef int (*fn_t)(int);

int id(int x)
{
    return x;
}

int main()
{
    fn_t tab[2];
    tab[0] = id;
    tab[1] = id;
    printf("%d\n", tab[0](3) + tab[1](4));
    return 0;
}
