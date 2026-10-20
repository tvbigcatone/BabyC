#include <stdio.h>
#include <string.h>
typedef struct { int id; char ita[24]; } Message;
Message g_m;
int main()
{
    char local[24];
    int ch;
    sprintf(g_m.ita, "id=%d", 5);
    snprintf(local, 24, "id=%d", 5); /* cache-friendly */
    ch = g_m.ita[0];
    printf("%s\n", g_m.ita);
    printf("%s\n", local);
    printf("%c\n", ch);
    return 0;
}
