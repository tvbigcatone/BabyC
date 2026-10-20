#include <stdio.h>
#include <string.h>
/* CROSS: memset + sprintf + global struct member via lib */
typedef struct { char a[8]; int n; } S;
S g;
int main()
{
    char local[8];
    int c;
    memset(local, 0, 8);
    local[0] = 'A';
    sprintf(g.a, "%d", 9);
    c = g.a[0];
    strcpy(local, g.a);
    printf("%s %s %c %d\n", g.a, local, c, g.a[0]);
    return 0;
}
