#include <stdio.h>
/* Anti-regression: struct S g = {{1,2,3}} nested brace for array member */
struct S { int arr[3]; int z; };
int main()
{
    struct S g = {{1, 2, 3}};
    printf("%d %d %d\n", g.arr[0], g.arr[1], g.arr[2]);
    struct S h = {{10, 20, 30}, 99};
    printf("%d %d %d %d\n", h.arr[0], h.arr[1], h.arr[2], h.z);
    return 0;
}
