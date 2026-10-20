/* Dereference type propagation through pointer arithmetic, multidimensional
 * arrays, floating pointees, and struct pointers. Values are compared to gcc. */
#include <stdio.h>

struct Item { int a; double d; };
static void showi(int x) { printf("i=%d\n", x); }
static void showd(double x) { printf("d=%.3f\n", x); }
static void showf(float x) { printf("f=%.3f\n", (double)x); }
static void showitem(struct Item *p) { printf("item=%d,%.3f\n", p->a, p->d); }

int main(void)
{
    int values[3] = {10, 20, 30};
    int *ip = values;
    int m[2][2] = {{31, 32}, {41, 42}};
    float fv = 3.25f;
    float *pf = &fv;
    float g = *pf;
    double dv = 1.25;
    double *pd = &dv;
    struct Item items[2] = {{51, 5.125}, {61, 6.125}};
    struct Item *p = items;
    struct Item *ps = &items[0];
    struct Item copy = *ps;
    char *fields[2] = {"field0", "field1"};

    showi(*(1 + ip));
    showi(**m);
    showf(g);
    *pd = 9;
    showd(dv);
    showd(*pd);
    showi((p + 1)->a);
    showd((*(p + 1)).d);
    showitem(ps);
    showi((*ps).a);
    printf("field=%s\n", *(fields + 1));
    printf("copy=%d,%.3f\n", copy.a, copy.d);
    return 0;
}
