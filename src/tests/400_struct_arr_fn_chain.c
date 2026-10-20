#include <stdio.h>
#include <string.h>
typedef struct { int id; char ita[16]; } Message;
Message g_m;

void set_ita(char *dst, const char *src)
{
    strcpy(dst, src);
}

void via_ptr(Message *m, const char *s)
{
    set_ita(m->ita, s);
    m->id = 7;
}

int main()
{
    Message *p;
    int i;
    char tmp[16];

    g_m.id = 0;
    strcpy(g_m.ita, "xxx");
    p = &g_m;
    via_ptr(p, "ciao");

    /* index write after */
    for (i = 0; i < 3; i++)
        tmp[i] = p->ita[i];
    tmp[3] = 0;

    printf("%d %s %s %c\n", g_m.id, g_m.ita, tmp, p->ita[0]);
    return 0;
}
