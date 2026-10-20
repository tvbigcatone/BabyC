#include <stdio.h>
#include <string.h>

struct S { char buf[8]; char *name; };
static char *global_ptrs[2] = {"g0", "g1"};
static char global_chars[2][8] = {"ga", "gb"};
static double global_doubles[2] = {1.25, 2.5};
static struct S global_s = {{'s','b','u','f',0}, "global-name"};

static void show(char *s) { printf("%s\n", s); }
static void showd(double d) { printf("%.2f\n", d); }
int main(int argc, char **argv)
{
    char *a[2] = {"l0", "l1"};
    char names[2][8] = {"la", "lb"};
    char **pp = a;
    char *p = a[1];
    struct S s = {{'l','b','u','f',0}, 0};
    struct S sa[2] = {{{0}, 0}, {{0}, 0}};
    struct S *ps = &sa[1];
    s.name = "local-name";
    strcpy(sa[0].buf, "a0"); strcpy(sa[1].buf, "a1");
    sa[0].name = "arr0"; sa[1].name = "arr1";
    show(argv[1]);
    show(a[1]); show(global_ptrs[0]); show(pp[0]); show(*pp); printf("%c\n", **pp);
    show(names[1]); show(global_chars[0]); printf("%c\n", names[0][1]);
    show(*(a + 1)); show((char *)names[0]);
    show(names[1]); showd(global_doubles[1]); showd(global_doubles[0]);
    show(s.buf); show(s.name); show(sa[1].name); show(ps->buf);
    (void)p;
    return 0;
}
