/* F8: uninit use must ProgramFail under SAFE (default ON). */
#include <stdio.h>
int main(void)
{
    int x;
    printf("%d\n", x);
    return 0;
}
