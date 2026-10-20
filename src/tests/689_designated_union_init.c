/* C99 designated initializer for union members inside struct. */
#include <stdio.h>
enum tipo_valore { INTERO, REALE, CARATTERE };
struct valore {
    enum tipo_valore tipo;
    union {
        int i;
        float f;
        char c;
    } dato;
};
int main(void)
{
    struct valore a = { INTERO, { .i = 42 } };
    struct valore b = { REALE, { .f = 2.71f } };
    struct valore c = { CARATTERE, { .c = 'Z' } };
    printf("%d %d\n", a.tipo, a.dato.i);
    printf("%d %.2f\n", b.tipo, b.dato.f);
    printf("%d %c\n", c.tipo, c.dato.c);
    return 0;
}
