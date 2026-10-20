#include <stdlib.h>
#include <stdio.h>
int main(void)
{
    long value = strtol("10", NULL, 1);
    printf("ERROR: invalid base was not diagnosed (%ld)\n", value);
    return 0;
}
