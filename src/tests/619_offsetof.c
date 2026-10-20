#include <stdio.h>
struct S { int a; char b; long c; };
struct Packed { char x; char y; int z; };
struct Node { int value; struct Node *next; };
struct Nested { struct S inner; int tail; };
int main() {
    printf("%d %d %d\n", (int)offsetof(struct S, a), (int)offsetof(struct S, b), (int)offsetof(struct S, c));
    printf("%d %d %d\n", (int)offsetof(struct Packed, x), (int)offsetof(struct Packed, y), (int)offsetof(struct Packed, z));
    printf("%d %d\n", (int)offsetof(struct Node, value), (int)offsetof(struct Node, next));
    printf("%d %d\n", (int)offsetof(struct Nested, inner), (int)offsetof(struct Nested, tail));
    {
        struct S s;
        s.a = 0; s.b = 0; s.c = 0;
        char *base = (char *)&s;
        *(int *)(base + offsetof(struct S, a)) = 42;
        *(char *)(base + offsetof(struct S, b)) = 90;
        printf("%d %d\n", s.a, (int)s.b);
    }
    return 0;
}
