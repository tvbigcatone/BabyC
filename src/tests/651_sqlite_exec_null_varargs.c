/* Intrinsic varargs: sqlite3_exec(..., NULL,NULL,NULL) on 64-bit must not SEGV. */
#include <stdio.h>
#include <sqlite3.h>
int main(void)
{
    sqlite3 *db = 0;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != 0) { printf("open fail\n"); return 1; }
    rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", 0, 0, 0);
    printf("rc=%d\n", rc);
    sqlite3_close(db);
    return rc != 0;
}
