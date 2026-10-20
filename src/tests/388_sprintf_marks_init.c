#include <stdio.h>
/* ISOLATED: sprintf must mark dest buffer initialized (no false uninit) */
int main()
{
    char buf[32];
    int s = 0;
    /* no prior write to buf */
    sprintf(buf, "%d", 42);
    s = buf[0];
    printf("%c %d\n", buf[0], s);
    return 0;
}
