#include <stdio.h>
/* W37: Index on a double or float pointer parameter was stamped INT: s = a[i] read the
 * IEEE bits as an integer and converted them. */
double first(double *a) { return a[1]; }
double pick(double *a, int i) { double x = a[i]; return x; }
double sumd(double *a, int n)
{
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++)
        s += a[i];
    return s;
}
double sumw(double *a, int n)
{
    double s = 0.0;
    int i = 0;
    while (i < n) { s = s + a[i]; i++; }
    return s;
}
double dot(double *a, double *b, int n)
{
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}
void scale(double *a, int n, double k)
{
    int i;
    for (i = 0; i < n; i++)
        a[i] = a[i] * k;
}
int main(void)
{
    double a[5], b[5];
    float f[3];
    double t = 0.0;
    int i;
    for (i = 0; i < 5; i++) { a[i] = i + 0.5; b[i] = 2.0 * i; }
    printf("%f %f\n", first(a), pick(a, 3));
    printf("%f %f\n", sumd(a, 5), sumw(a, 5));
    printf("%f\n", dot(a, b, 5));
    scale(a, 5, 1.5);
    printf("%f %f\n", a[0], a[4]);
    f[0] = 1.5f; f[1] = 2.5f; f[2] = 3.0f;
    for (i = 0; i < 3; i++) t += f[i];
    printf("%f\n", t);
    return 0;
}
