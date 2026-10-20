#include <stdio.h>
/* pointer param present; body does not deref — Body IR should engage */
int tag(char *s, int n)
{
    return n + 1;
}
void main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 5; i++)
        s = s + tag("x", i);
    printf("s %d\n", s);
}
