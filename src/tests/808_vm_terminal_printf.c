/* The only admitted host call is a terminal printf after all VM work. */
#include <stdio.h>
static int values[4];

int main(void)
{
    int i;
    i = 0;
    while (i < 4)
    {
        values[i] = i + 2;
        i++;
    }
    printf("%d %d %d %d\n", values[0], values[1], values[2], values[3]);
    return 0;
}
