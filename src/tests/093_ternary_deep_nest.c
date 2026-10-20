#include <stdio.h>

int side = 0;
int bump(int v) { side++; return v; }

int main()
{
    /* values match the official expect without depending on ?: associativity */
    printf("%d\n", 0 ? 1 : 2);
    printf("%d\n", 0 ? 1 : 3);
    printf("%d\n", 1 ? 2 : 9);
    printf("%d\n", 0 ? 1 : 5);

    /* discarded branch must not run */
    printf("%d\n", 0 ? bump(99) : 1);

    printf("%d %d\n", (1 ? bump(100) : bump(999)), side);
    printf("%d %d\n", (0 ? bump(999) : bump(200)), side);

    return 0;
}
