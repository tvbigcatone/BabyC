#include <stdio.h>
int main()
{
    printf("%5d\n", 42);
    printf("%05d\n", 42);
    printf("%-5d\n", 42);
    printf("%.2f\n", 3.14159);
    printf("%8.2f\n", 3.14159);
    printf("%.0f\n", 3.6);
    return 0;
}
