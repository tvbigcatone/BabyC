#include <stdlib.h>
#include <stdio.h>

int main(void)
{
    int values[2] = { 2, 1 };
    puts("before");
    qsort(values, 2, sizeof(values[0]), 0);
    puts("ERROR: null qsort comparator was not diagnosed");
    return 0;
}
