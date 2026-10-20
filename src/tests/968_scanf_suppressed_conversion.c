#include <stdio.h>

int main(void)
{
    int value = 0;
    int converted = sscanf("12 34", "%*d%d", &value);
    printf("converted=%d value=%d\n", converted, value);
    return 0;
}
