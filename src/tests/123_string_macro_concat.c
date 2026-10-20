/* Adjacent string concatenation after object-like macros expand to string
 * literals (C translation phase 6). Used e.g. for ANSI colour helpers:
 *   printf(COL_RED "ERRORE" COL_RESET ": %s\n", msg);
 * Previously could crash (heap stack assert) or fail with
 * "value not expected here". */

#include <stdio.h>

#define COL_RED    "[RED]"
#define COL_GREEN  "[GREEN]"
#define COL_RESET  "[/]"
#define PREFIX     "ERR: "

int main()
{
    /* macro + literal + macro as printf format */
    printf(COL_RED "ERRORE" COL_RESET "\n");

    /* literal + macro + literal */
    printf("X" COL_GREEN "Y" COL_RESET "Z\n");

    /* macro + macro */
    printf(PREFIX COL_RED "fail" COL_RESET "\n");

    /* concatenated piece used as argument, not only as format */
    printf("%s\n", COL_RED "msg" COL_RESET);

    /* longer chain */
    printf(COL_RED "A" COL_GREEN "B" COL_RESET "C\n");

    return 0;
}
