#include <stdio.h>

struct Item { int id; const char *name; };
static struct Item item = {7, "seven"};
static int values[3] = {11, 22, 33};
static int calls;
static struct Item *g(void) { ++calls; return &item; }
static int *h(void) { ++calls; return values; }

int main(void)
{
    int i = 0;
    printf("%s\n", g()->name);
    printf("calls=%d\n", calls);
    printf("%d\n", h()[1]);
    printf("calls=%d\n", calls);
    printf("%d\n", *h());
    printf("calls=%d\n", calls);
    printf("%d\n", values[i++]);
    printf("i=%d calls=%d\n", i, calls);
    return 0;
}
