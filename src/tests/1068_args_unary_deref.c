/* Unary '*' arguments to user-defined functions: the dereferenced value is
 * an ordinary by-value argument (scalar or pointer). Covers pointer locals,
 * multi-level pointers, array decay, array-of-pointer elements, double and
 * char pointees, pointer typedefs and single evaluation of side effects. */
#include <stdio.h>

typedef double real;
typedef int *IP;

static int calls;
static int vals[3] = {41, 42, 43};

static void showi(int v) { printf("i %d\n", v); }
static void showl(long long v) { printf("l %lld\n", v); }
static void showd(double v) { printf("d %.3f\n", v); }
static void showc(char c) { printf("c %c\n", c); }
static void shows(char *s) { printf("s %s\n", s); }
static void showip(int *p) { printf("p %d\n", *p); }
static int *next_val(void) { return &vals[calls++]; }

int main(void)
{
    int x = 5;
    int *px = &x;
    int **ppx = &px;
    char *a[2] = {"l0", "l1"};
    char **pp = a;
    double dd = 2.5;
    double *pd = &dd;
    double da[3] = {0.5, 1.5, 2.5};
    double *dq = da;
    char str[4] = "xyz";
    char *pc = str;
    char *t = str;
    int arr[3] = {7, 8, 9};
    int *ip = arr;
    unsigned char ub[2] = {200, 201};
    unsigned char *ucp = ub;
    long long big = 1LL << 40;
    long long *lp = &big;
    real r = 7.25;
    real *rp = &r;
    IP q = arr;
    int k = 0;
    double v;

    showi(*px);
    showi((*px));
    showi(**ppx);
    showip(*ppx);
    shows(*a);
    shows(*(a + 1));
    shows(*pp);
    shows(*(pp + 1));
    showc(**pp);
    showd(*pd);
    v = *pd;
    showd(v);
    showd(*da);
    showd(*(da + 2));
    showd(*dq++);
    showd(*dq);
    showc(*pc);
    showc(*str);
    showc(*(str + 2));
    showc(*t++);
    showc(*t);
    showi(*ip++);
    showi(*ip);
    showi(*++ip);
    showi(*(arr + 1));
    showi(*(ip - 1));
    showi(*(ip + k++));
    showi(k);
    showi(*ucp);
    showi(*(ucp + 1));
    showl(*lp);
    showd(*rp);
    showi(*q);
    showi(*next_val());
    showi(*next_val());
    showi(calls);
    showi(*px + *ip);
    return 0;
}
