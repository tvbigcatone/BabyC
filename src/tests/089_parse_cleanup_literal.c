#include <stdio.h>

int main()
{
    /* Esegue codice definito in stringhe letterali passate a PicocParse
     * con CleanupSource=FALSE (default per startup/main).
     * Se ParseCleanup libera erroneamente queste stringhe, crash. */
    printf("main_ran=ok\n");
    printf("cleanup_literal=ok\n");
    return 0;
}