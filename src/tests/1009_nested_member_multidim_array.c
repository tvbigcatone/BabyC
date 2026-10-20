#include <stdio.h>

struct MatrixPart {
    char tag;
    int cells[2][3];
};

struct Envelope {
    char prefix;
    struct MatrixPart part;
};

void main(void)
{
    struct Envelope e;
    int *p;

    e.prefix = 1;
    e.part.tag = 2;
    p = &e.part.cells[1][2];
    *p = 60;
    e.part.cells[1][2]++;
    printf("%d %d %d\n", e.prefix, e.part.tag, *p);
}
