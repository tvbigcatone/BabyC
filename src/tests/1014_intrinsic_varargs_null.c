/* Registered varargs intrinsics: null tails must preserve a null pointer on linear. */
#include <stdio.h>
#include <sqlite3.h>
int main(void)
{
    sqlite3 *db = 0;
    int rc = sqlite3_open(":memory:", &db);
    printf("open=%d\n", rc);
    if (rc != 0) return 1;
    rc = sqlite3_exec(db, "CREATE TABLE t(v INTEGER);", 0, 0, 0);
    printf("zero=%d\n", rc);
    rc = sqlite3_exec(db, "INSERT INTO t VALUES(7);", NULL, NULL, NULL);
    printf("null=%d\n", rc);
    rc = sqlite3_close(db);
    printf("close=%d\n", rc);
    return 0;
}
