#include <stdio.h>
int main(void)
{
    long double value = 0.0;
    if (sscanf("1.25", "%Lf", &value) != 1)
        return 1;
    if (value < 1.24 || value > 1.26)
        return 2;
    puts("ok");
    return 0;
}
