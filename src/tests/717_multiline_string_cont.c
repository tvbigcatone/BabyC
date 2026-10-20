/* GNU-style string line continuation (coreutils cat.c pattern) */
#include <stdio.h>
int main(void)
{
    printf("\
hello\n\
world\n");
    printf("%s", "\
a\
b\
c\n");
    return 0;
}
