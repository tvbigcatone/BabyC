#include <stdio.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define SQ(x) ((x) * (x))
#define WRAP(p) ((p) != 0 ? (p) : -1)

void main()
{
    int a = 3;
    int b = 5;
    printf("%d\n", MAX(a, b));
    printf("%d\n", SQ(a + 1));
    printf("%d\n", WRAP(0));
    printf("%d\n", WRAP(9));
}
