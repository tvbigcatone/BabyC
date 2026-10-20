#include <stdio.h>
#include <string.h>
int main()
{
    char ok[8];
    strcpy(ok, "hi");
    printf("%s\n", ok);
    memset((void *)0x10, 1, 1);
    return 0;
}
