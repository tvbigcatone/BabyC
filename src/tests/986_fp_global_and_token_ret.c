#include <stdio.h>
/* double/float globals read-modify-written in loops and leaf functions
 * (linear register encoding = IEEE bits), narrow globals, and a token-path
 * caller (multi-declarator struct init keeps show() on the token path) of
 * Body-IR functions returning double. */
struct P { int x, y; };
double gd = 1.5;
float gf = 2.0f;
char gc = 'a';
short gs = 7;
double step(int i) { gd = gd * 2.0 + i; gc = gc + 1; gs = gs * 3; return gd; }
double half(double w) { return w / 2.0; }
void show(void)
{
    struct P p1 = {10, 20}, p2 = {30, 40};
    double h = half(5.0);
    printf("%.2f %.2f %d %d\n", h + half(1.0), step(0), p1.x, p2.y);
}
int main(void)
{
    int i;
    double t = 0;
    for (i = 0; i < 4; i++)
    {
        t += step(i);
        gf = gf + 0.25f;
    }
    printf("%.2f %.2f %.2f %c %d\n", t, gd, gf, gc, gs);
    show();
    return 0;
}
