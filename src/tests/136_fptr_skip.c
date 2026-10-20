/* Bug #5: skip di chiamata via puntatore in RunModeSkip desincronizza
 * il token stream. Il test esercita il percorso in modalita' RunModeRun
 * e verifica che il parser non si desincronizzi. */

#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int mul(int a, int b)
{
    return a * b;
}

void main()
{
    int (*f)(int, int);
    int r;

    f = add;
    r = f(3, 4);
    printf("%d\n", r);          /* 7 */

    f = mul;
    r = f(3, 4);
    printf("%d\n", r);          /* 12 */

    /* Chiamata in un ramo non preso (RunModeSkip) */
    if (0)
    {
        r = f(100, 200);
    }

    /* Chiamata in un ramo preso */
    if (1)
    {
        r = f(5, 6);
    }
    printf("%d\n", r);          /* 30 */

    printf("done\n");
}