#include <stdio.h>

int main()
{
    int arr[3] = {10, 20, 30};
    int *p = 0;

    printf("%d\n", arr ? 1 : 0);
    printf("%d\n", p ? 1 : 0);
    printf("%d\n", arr[1] > 15 ? arr[1] : arr[0]);
    printf("%d\n", p ? *p : 99);
    return 0;
}
