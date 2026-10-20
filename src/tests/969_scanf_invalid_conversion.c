#include <stdio.h>

int main(void)
{
    int value = 0;
    int converted;
    puts("before");
    converted = sscanf("42", "%q", &value);
    printf("ERROR: invalid scanf conversion was not diagnosed (%d)\n", converted);
    return 0;
}
