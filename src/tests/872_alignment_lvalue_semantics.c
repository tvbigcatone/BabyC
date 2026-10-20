#include <stdio.h>

struct Pair { int first; int second; };
struct Inner { char tag; int value; };
struct Outer { char prefix; struct Inner inner; };

struct Pair p;
struct Outer o;
int *alias;

p.first = 3;
p.second = 7;
p.second += 5;
p.second++;
alias = &p.second;
*alias = 41;
(*alias)++;
printf("%d %d\n", p.first, p.second);

o.inner.tag = 9;
o.inner.value = 17;
o.inner.value *= 2;
printf("%d %d\n", o.inner.tag, o.inner.value);

void main() {}
