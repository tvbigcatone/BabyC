/* Anti-regression: global struct array member via . and strcpy */
#include <stdio.h>
#include <string.h>
typedef struct {
    int id;
    char ita[16];
} Message;
Message g_m;
int main() {
    Message *p;
    g_m.ita[0] = 'c';
    g_m.ita[1] = 'i';
    g_m.ita[2] = 'a';
    g_m.ita[3] = 'o';
    g_m.ita[4] = '\0';
    printf("%s\n", g_m.ita);
    strcpy(g_m.ita, "ok");
    printf("%s\n", g_m.ita);
    p = &g_m;
    p->id = 7;
    printf("%d %s\n", p->id, p->ita);
    return 0;
}
