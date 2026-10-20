/* Anti-regression: Param T* array member decay for strcpy under vm_strict. */
#include <stdio.h>
#include <string.h>
typedef struct { int id; char ita[16]; } Message;
void via(Message *m, const char *s) {
    strcpy(m->ita, s);
    m->id = 7;
}
int main(void) {
    Message g;
    g.id = 0;
    strcpy(g.ita, "xxx");
    via(&g, "ciao");
    printf("%d %s\n", g.id, g.ita);
    return 0;
}
