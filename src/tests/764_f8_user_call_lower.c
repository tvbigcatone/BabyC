/* F8: user function call lowers under BABYC_EXEC=both (stdout). */
#include <stdio.h>
int add(int a, int b) { return a + b; }
int main(void)
{
    int i, s = 0;
    for (i = 0; i < 5; i++)
        s = add(s, i);
    printf("%d\n", s);
    return 0;
}
