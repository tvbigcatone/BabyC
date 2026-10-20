#include <stdio.h>
enum E { A, B };
/* const enum local: the enum declaration path must record const. */
int main() {
    const enum E e = A;
    e = B;
    printf("%d\n", e);
    return 0;
}
