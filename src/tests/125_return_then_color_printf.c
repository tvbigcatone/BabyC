/* Regression: after return, remaining body is parsed in non-Run mode.
 * COL_RED "ERRORE" COL_RESET must be consumable without
 * "value not expected here" (messages_manager.c pattern). */

#include <stdio.h>

#define COL_RED   "\033[31m"
#define COL_GREEN "\033[32m"
#define COL_RESET "\033[0m"

int do_work(int fail_early)
{
    if (fail_early) {
        printf("ERRORE: data non trovato: %s\n", "messagesData.csv");
        return 1;
    }
    printf(COL_RED "ERRORE" COL_RESET ": non in DB: %s\n", "name");
    printf("  Esito            : " COL_GREEN "OK" COL_RESET "\n");
    return 0;
}

int main()
{
    printf("%d\n", do_work(1));
    printf("%d\n", do_work(0));
    return 0;
}
