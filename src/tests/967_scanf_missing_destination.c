#include <stdio.h>

int main(void)
{
    int result;
    puts("before");
    result = sscanf("42", "%d");
    printf("ERROR: missing scanf destination was not diagnosed (%d)\n", result);
    return 0;
}
