#include <stdio.h>
/* CROSS: comma operator + nested struct brace init + (a,b,c) call-arg */
struct S { int arr[3]; int z; };
int main()
{
    int a, b, c;
    struct S g = {{1, 2, 3}, 9};
    a = 0, b = 0;
    for (a = 0, b = 0; a < 2; a++, b += 3)
        c = (a, b);
    printf("%d %d %d\n", g.arr[0], g.arr[1], g.arr[2]);
    printf("%d\n", g.z);
    printf("%d %d %d\n", a, b, c);
    printf("%d\n", (a, b, c));
    return 0;
}
