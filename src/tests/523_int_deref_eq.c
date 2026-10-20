/* Fuse *p==*q remains valid for int* (pointee size 4). */
#include <stdio.h>

int same_int(int *a, int *b)
{
    return *a == *b;
}

int main()
{
    int x = 42, y = 42, z = 7;
    printf("%d\n", same_int(&x, &y));
    printf("%d\n", same_int(&x, &z));
    x = 7;
    printf("%d\n", same_int(&x, &z));
    return 0;
}
