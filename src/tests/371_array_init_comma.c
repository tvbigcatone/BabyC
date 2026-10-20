#include <stdio.h>
/* Anti-regression: array {1,2,3} must not collapse via comma operator */
int main()
{
    int a[3] = {1, 2, 3};
    printf("%d %d %d\n", a[0], a[1], a[2]);
    return 0;
}
