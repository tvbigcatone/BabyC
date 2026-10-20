/* Anti-regression: *p == *q on char* at end-of-string and mid-string.
 * Must compare one byte, not a 4-byte int load. */
#include <stdio.h>

int same_at(const char *a, const char *b)
{
    return *a == *b;
}

int walk_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main()
{
    const char *s = "updateDB";
    const char *t = "updateDB";
    const char *u = "updateXX";
    char local[16];
    int i;

    printf("%d\n", same_at(s, t));
    printf("%d\n", same_at(s + 8, t + 8)); /* both NUL */
    printf("%d\n", same_at(s + 6, u + 6)); /* 'D' vs 'X' */
    printf("%d\n", walk_eq(s, t));
    printf("%d\n", walk_eq(s, u));

    for (i = 0; i < 9; i++)
        local[i] = s[i];
    printf("%d\n", walk_eq(local, s));
    return 0;
}
