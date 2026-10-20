#include "stdio.h"

void main()
{
    int y = 14;
    double low = (y < 13.5) ? y : 13.5;
    double high = (y > 13.5) ? y : 13.5;
    double converted = 2;
    int truncated = 3.14;
    printf("%f %f %f %d\n", low, high, converted, truncated);
}
