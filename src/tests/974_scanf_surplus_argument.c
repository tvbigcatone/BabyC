#include <stdio.h>
int main(void)
{
    int value = 0;
    int converted = sscanf("42", "%d", &value, 123);
    if (converted != 1 || value != 42)
        return 1;
    puts("ok");
    return 0;
}
