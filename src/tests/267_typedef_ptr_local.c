#include <stdio.h>

/* Locals whose type is a typedef-to-struct (FILE, user typedefs) must not
 * abort during Body IR compile. IrCompileStmt used to parse `FILE *f` as the
 * expression `FILE * f` and fail with "'f' is undefined". */
typedef struct Pair { int a; int b; } Pair;

int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

void main()
{
    Pair *p;
    Pair s;
    s.a = 3;
    s.b = 4;
    p = &s;
    printf("pair %d %d\n", p->a, p->b);
    printf("exists %d\n", file_exists("267_typedef_ptr_local.c"));
    printf("missing %d\n", file_exists("no_such_file_zzz"));
}
