#include <stdio.h>
void foo(void) {
    while (1) { int i = 5; break; }
    printf("after while\n");
    for (int i = 0; i < 3; i++)
        printf("%d\n", i);
    printf("done\n");
}
foo();
void main(){}
