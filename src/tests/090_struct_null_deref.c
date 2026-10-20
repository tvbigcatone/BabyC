#include <stdio.h>

struct Point {
    int x;
    int y;
};

int main()
{
    struct Point p;
    p.x = 10;
    p.y = 20;

    struct Point *pp = &p;
    printf("%d\n", pp->x);
    printf("%d\n", pp->y);

    /* void* usato come puntatore a struct: se il parser non valida
     * FromType, si ha NULL deref nell'accesso al membro */
    void *vp = &p;
    struct Point *sp = (struct Point *)vp;
    printf("%d\n", sp->x);

    /* puntatore a struct inizializzato a NULL: -> deve fallire pulitamente
     * ma solo dopo un check esplicito, non con un crash del processo */
    struct Point *np = 0;
    if (np != 0)
        printf("%d\n", np->x);
    else
        printf("null_struct_ok\n");

    return 0;
}