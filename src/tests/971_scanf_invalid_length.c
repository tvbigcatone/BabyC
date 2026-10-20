#include <stdio.h>
int main(void)
{
    char text[8];
    int converted = sscanf("hello", "%Ls", text);
    printf("ERROR: invalid scanf length was not diagnosed (%d)\n", converted);
    return 0;
}
