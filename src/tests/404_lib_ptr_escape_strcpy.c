#include <stdio.h>
#include <string.h>
/* ISOLATED: any lib call with pointer dest marks buffer initialized */
int main()
{
    char buf[16];
    /* no prior init of buf */
    strcpy(buf, "ok");
    printf("%c %s\n", buf[0], buf);
    return 0;
}
