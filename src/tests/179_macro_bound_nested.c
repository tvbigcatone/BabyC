#include <stdio.h>
#define DISKS 3
int A[DISKS], B[DISKS];
int main()
{
    int i;
    for (i = 0; i < DISKS; i = i + 1)
        A[i] = i + 1;
    for (i = 0; i < DISKS; i = i + 1)
        B[i] = 0;
    for (i = 0; i < DISKS; i = i + 1)
        printf("%d ", A[i]);
    printf("\n");
    return 0;
}
