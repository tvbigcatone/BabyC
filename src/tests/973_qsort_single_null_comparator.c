#include <stdlib.h>
#include <stdio.h>
int main(void)
{
    int value = 7;
    qsort(&value, 1, sizeof(value), NULL);
    printf("ERROR: null comparator was not diagnosed (%d)\n", value);
    return 0;
}
