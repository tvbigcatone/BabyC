#include <stdio.h>

int g;
int arr[4];
struct S { int x; int y; };
struct S gs;

int *ret_global(void) { return &g; }
int *ret_arr_elem(int i) { return &arr[i]; }
struct S *ret_struct(void) { return &gs; }

void write_through(int *p, int v) { *p = v; }

int *identity(int *p) { return p; }

int main() {
    int *p;
    int local;
    /* return &global, write through */
    p = ret_global();
    *p = 11;
    printf("%d\n", g);
    /* return &arr[i] */
    p = ret_arr_elem(2);
    *p = 22;
    printf("%d\n", arr[2]);
    /* return &struct, member dest */
    p = &ret_struct()->x;
    *p = 33;
    ret_struct()->y = 44;
    printf("%d %d\n", gs.x, gs.y);
    /* identity chain */
    local = 0;
    p = identity(&local);
    *p = 55;
    printf("%d\n", local);
    /* write_through out-param */
    write_through(&local, 66);
    printf("%d\n", local);
    write_through(ret_arr_elem(0), 77);
    printf("%d\n", arr[0]);
    return 0;
}
