/* Cross: const char* ++ (233-class) + recursion + switch */
#include <stdio.h>

int scan(const char *s, int mode)
{
    int c, n = 0;
    switch (mode) {
        case 0:
            while ((c = *s++))
                n = n + c;
            return n;
        case 1:
            if (!*s)
                return 0;
            return *s + scan(s + 1, 1);
        default:
            return scan(s, 0) + scan(s, 1);
    }
}

int main()
{
    printf("%d %d %d\n", scan("ab", 0), scan("ab", 1), scan("ab", 2));
    return 0;
}
