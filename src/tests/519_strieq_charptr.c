/* Anti-regression: char* walk + *a==*b at NUL.
 * G6 fuse used to load *char as int, so str_ieq("updateDB","updateDB")
 * failed (messages_manager parse_action). */
#include <stdio.h>
#include <ctype.h>

int str_ieq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == *b;
}

int parse_action(const char *s)
{
    if (str_ieq(s, "extractFromDB")) return 1;
    if (str_ieq(s, "updateDB"))      return 2;
    return 0;
}

int main()
{
    printf("%d\n", str_ieq("updateDB", "updateDB"));
    printf("%d\n", str_ieq("UpdateDB", "updateDB"));
    printf("%d\n", str_ieq("updateDB", "updateD"));
    printf("%d\n", str_ieq("update", "updateDB"));
    printf("%d\n", parse_action("updateDB"));
    printf("%d\n", parse_action("extractFromDB"));
    printf("%d\n", parse_action("nope"));
    return 0;
}
