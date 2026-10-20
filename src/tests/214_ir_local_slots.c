/* Locals in IR frame: sum 0..9 without relying on VariableGet for s,i */
#include <stdio.h>
int sum10(void)
{
    int i;
    int s;
    s = 0;
    for (i = 0; i < 10; i = i + 1)
        s = s + i;
    return s;
}
int main()
{
    printf("%d\n", sum10());
    return 0;
}
