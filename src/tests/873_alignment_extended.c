#include <stdio.h>

struct Wide { char tag; volatile int vcount; double amount; void *ptr; int count; };
struct Bits { unsigned int low:3; unsigned int high:5; };
union Number { int i; double d; };
struct Nest { char c; struct Wide wide; struct Bits bits; union Number number; };

int target;
struct Nest n;
volatile int volatile_source;

void main()
{
    n.c = 1;
    n.wide.vcount = 8;
    n.wide.vcount++;
    n.wide.amount = 3.25;
    n.wide.amount += 1.5;
    n.wide.ptr = &target;
    n.wide.count = 4;
    n.wide.count++;
    n.bits.low = 5;
    n.bits.high = 17;
    n.number.i = 23;
    n.number.i += 2;
    target = 41;
    *(int *)n.wide.ptr += 1;
    volatile_source = target;
    volatile_source++;
    printf("%d %d %.2f %d %d %d %d\n", n.c, n.wide.vcount, n.wide.amount, n.wide.count, n.bits.low, n.bits.high, n.number.i);
    printf("%d %d\n", target, volatile_source);
}
