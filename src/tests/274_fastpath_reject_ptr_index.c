#include <stdio.h>

/* Anti-regression: Irb accepts a[i]/p[i] only when the element type is
 * int-base (IrbBind: IrbIsIntBase(Elem)).  An array of pointers has
 * TypePointer elements, so indexing it must be refused and fall back
 * to the token / Loop Body IR path with correct results. */

void main()
{
    char *names[3];
    char n0[5], n1[6], n2[7];
    char *w;
    int i, len, t;

    n0[0] = 'a'; n0[1] = 'b'; n0[2] = 'c'; n0[3] = 'd'; n0[4] = 0;
    n1[0] = 'e'; n1[1] = 'f'; n1[2] = 'g'; n1[3] = 'h'; n1[4] = 'i'; n1[5] = 0;
    n2[0] = 'j'; n2[1] = 'k'; n2[2] = 'l'; n2[3] = 'm'; n2[4] = 'n'; n2[5] = 'o'; n2[6] = 0;

    names[0] = n0;
    names[1] = n1;
    names[2] = n2;

    t = 0;
    for (i = 0; i < 3; i++) {
        /* names[i] itself: index into an array of pointers, Elem == TypePointer
         * fails IrbIsIntBase, so IrbBind refuses this site and the whole
         * for-loop runs on the token / Loop Body IR path instead. */
        w = names[i];
        len = 0;
        while (w[len] != 0)
            len++;
        t = t + len;
    }

    printf("t %d\n", t);
}
