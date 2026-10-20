#include <stdio.h>
#include <string.h>
struct S { char buf[8]; };
static struct S sa[3] = {{"a"}, {"b"}, {"c"}};
static int calls;
static int k(void) { ++calls; return 2; }
int main(void)
{
    int i = 0;
    char *p = sa[1].buf;
    printf("%s\n", p);
    strcpy(sa[1].buf, "z");
    printf("%s\n", sa[1].buf);
    strcpy(sa[i++].buf, "x");
    printf("i=%d value=%s\n", i, sa[0].buf);
    strcpy(sa[k()].buf, "y");
    printf("calls=%d value=%s\n", calls, sa[2].buf);
    return 0;
}
