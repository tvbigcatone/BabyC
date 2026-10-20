#include <stdio.h>

struct MisalignedArrayStorage {
    char prefix;
    int values[3];
};

struct Nested {
    char marker;
    struct MisalignedArrayStorage item;
};

void main(void)
{
    struct MisalignedArrayStorage s;
    struct Nested n;
    int *p;

    s.prefix = 1;
    s.values[0] = 10;
    s.values[1] = 20;
    s.values[2] = 30;
    p = &s.values[1];
    *p += 2;

    n.marker = 7;
    n.item.prefix = 8;
    n.item.values[0] = 40;
    n.item.values[1] = 50;
    n.item.values[2] = 60;
    n.item.values[2]++;

    printf("%d %d %d %d\n", s.values[0], s.values[1], s.values[2], n.item.values[2]);
}
