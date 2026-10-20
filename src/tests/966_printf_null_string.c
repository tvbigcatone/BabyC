#include <stdio.h>

int main(void)
{
    char *text = 0;
    puts("before");
    printf("%s", text);
    puts("ERROR: NULL string was not diagnosed");
    return 0;
}
