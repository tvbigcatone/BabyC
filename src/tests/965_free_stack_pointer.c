#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int value = 7;
    puts("before");
    free(&value);
    puts("ERROR: freeing a stack pointer was not diagnosed");
    return 0;
}
