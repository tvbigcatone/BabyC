#include <stdio.h>
int main() {
    int x = 0;
    /* simple forward */
    goto L1;
    x = 99;
L1:
    x = x + 1;
    printf("%d\n", x);

    /* forward over dead block */
    goto L2;
    { int dead = 7; x = dead; }
L2:
    x = x + 10;
    printf("%d\n", x);

    /* chain A -> B -> C */
    goto A;
B:
    x = x + 100;
    goto C;
A:
    x = x + 2;
    goto B;
C:
    printf("%d\n", x);

    /* forward then back */
    goto D;
E:
    x = x + 3;
    goto F;
D:
    x = x + 4;
    goto E;
F:
    printf("%d\n", x);

    /* cleanup style */
    {
        int ok = 1;
        if (ok)
            goto ok_lab;
        printf("fail\n");
        return 1;
ok_lab:
        printf("ok\n");
    }

    /* consecutive labels */
    goto G;
G:
H:
I:
    x = x + 1;
    printf("%d\n", x);

    /* backward goto (regression) */
    {
        int n = 0;
    loop:
        n = n + 1;
        if (n < 3)
            goto loop;
        printf("%d\n", n);
    }
    return 0;
}
