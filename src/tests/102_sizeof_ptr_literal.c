/* sizeof("literal") is the array size including NUL.
 * sizeof(char *p) where p points at a literal must be sizeof(char*),
 * NOT the string length.
 *
 * Use a long enough literal so that (strlen+1) != sizeof(char*) on both
 * 32-bit and 64-bit platforms (avoids 4==4 coincidence on 32-bit). */

#include <stdio.h>

void main()
{
    /* 15 chars + NUL = 16, which is never equal to sizeof(char*) (4 or 8) */
    char *p = "hello_world_123";
    int sz_lit = (int)sizeof("hello_world_123");
    int sz_p = (int)sizeof(p);
    int sz_type = (int)sizeof(char *);
    int sz_empty = (int)sizeof("");

    printf("%d\n", sz_lit);                  /* 16 */
    printf("%d\n", sz_p == sz_type);         /* 1 */
    printf("%d\n", sz_p != sz_lit);          /* 1 — must not be string length */
    printf("%s\n", p);                       /* hello_world_123 */
    printf("%d\n", sz_empty);                /* 1 */
}
