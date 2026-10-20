#include <stdio.h>

struct S { int x; };

int main()
{
    int a = 1;
    int *p = &a;
    void *vp = &a;
    struct S s;
    s.x = 42;

    /* Ternario con puntatori di tipo diverso: il ramo non preso
     * produce un valore void che deve essere gestito correttamente */
    printf("%d\n", p ? *p : 0);

    /* Ternario con array (decade a puntatore) */
    int arr[3] = {1, 2, 3};
    int *q = arr ? arr : 0;
    printf("%d\n", *q);

    /* Ternario con void* */
    void *r = vp ? vp : 0;
    printf("%d\n", *(int *)r);

    /* Ternario con struct* */
    struct S *sp = &s;
    struct S *sp2 = sp ? sp : 0;
    printf("%d\n", sp2->x);

    /* Ternario con tipi misti: int vs pointer */
    int cond = 1;
    long long v = cond ? (long long)(int *)&a : 0LL;
    printf("%d\n", v != 0);

    return 0;
}