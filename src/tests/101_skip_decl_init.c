#include <stdio.h>

/* Regression: declarations with initialisers must be parseable in RunModeSkip
 * (dead if/else branches, for-loop prescan, etc.). Previously NewVariable was
 * NULL and ParseDeclarationAssignment did NewVariable->Typ->Base → crash. */

struct Point {
    int x;
    int y;
};

int main()
{
    int reached = 0;

    /* --- simple scalar init in a dead branch --- */
    if (0) {
        int y = 42;
        printf("dead_scalar=%d\n", y);
    }
    reached++;

    /* --- array brace init in a dead branch --- */
    if (0) {
        int arr[3] = {1, 2, 3};
        printf("dead_arr=%d\n", arr[0]);
    }
    reached++;

    /* --- struct brace init in a dead branch --- */
    if (0) {
        struct Point p = {10, 20};
        printf("dead_struct=%d\n", p.x);
    }
    reached++;

    /* --- else branch skipped while then runs --- */
    if (1) {
        int live = 7;
        printf("live=%d\n", live);
    } else {
        int dead = 99;
        int dead_arr[2] = {5, 6};
        printf("should_not=%d\n", dead + dead_arr[0]);
    }
    reached++;

    /* --- for-loop body with local inits (body runs; also exercises scoping) --- */
    {
        int i;
        for (i = 0; i < 2; i++) {
            int x = i + 1;
            int a[2] = {i, i + 10};
            printf("loop=%d,%d\n", x, a[1]);
        }
    }
    reached++;

    /* --- nested dead if with multiple declarators --- */
    if (0) {
        int a = 1, b = 2, c = 3;
        printf("dead_multi=%d\n", a + b + c);
    }
    reached++;

    printf("reached=%d\n", reached);
    printf("skip_decl_init_ok\n");
    return 0;
}
