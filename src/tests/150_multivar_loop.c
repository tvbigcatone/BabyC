/* Multi-variable loop body: exercises multi-slot VariableGet cache */
#include <stdio.h>
int main()
{
    int i, a, b, c, s;
    a = 1; b = 2; c = 3; s = 0;
    for (i = 0; i < 10; i = i + 1)
        s = s + a + b + c + i;
    printf("%d\n", s);
    return 0;
}
