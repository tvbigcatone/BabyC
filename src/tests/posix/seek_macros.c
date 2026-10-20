/* SEEK_* must be visible from unistd.h alone (not only stdio platform vars). */
#include <unistd.h>

int main(void)
{
    int set = SEEK_SET;
    int cur = SEEK_CUR;
    int end = SEEK_END;
    if (set != 0) { write(STDOUT_FILENO, "FAIL: SEEK_SET\n", 15); return 1; }
    if (cur != 1) { write(STDOUT_FILENO, "FAIL: SEEK_CUR\n", 15); return 2; }
    if (end != 2) { write(STDOUT_FILENO, "FAIL: SEEK_END\n", 15); return 3; }
    write(STDOUT_FILENO, "BabyPOSIX SEEK macros: ok\n", 26);
    return 0;
}
