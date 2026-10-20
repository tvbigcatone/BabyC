#include <stdio.h>

int main()
{
    double v = 3.14159265;
    int prec;
    for (prec = 0; prec <= 4; prec++)
        printf("p=%d val=%.*f\n", prec, prec, v);
    printf("width+prec %*.*f\n", 10, 3, v);
    return 0;
}
