/* Same concatenation with ANSI escape sequences in the macros. */

#include <stdio.h>

#define COL_RED   "\033[31m"
#define COL_RESET "\033[0m"

int main()
{
    /* Must not crash; print contains the visible text */
    printf(COL_RED "ERRORE" COL_RESET ": data non trovato: %s\n", "messagesData.csv");
    return 0;
}
