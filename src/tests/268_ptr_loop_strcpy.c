#include <stdio.h>

/* Regression: Body IR must not run on functions with pointer params.
 * Miscompiled *p / p++ loops hung forever (update_messages / split_csv). */

static void safe_strcpy(char *dst, const char *src, int dst_size)
{
    int i;
    if (dst == 0 || dst_size <= 0)
        return;
    if (src == 0) {
        dst[0] = 0;
        return;
    }
    for (i = 0; i < dst_size - 1 && src[i] != 0; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static int split_csv(char *line, char **out, int max)
{
    int i;
    char *p;
    for (i = 0; i < max; i++)
        out[i] = 0;
    i = 0;
    p = line;
    while (i < max) {
        out[i] = p;
        while (*p != 0 && *p != 59) /* 59 = ';' */
            p++;
        if (*p == 59) {
            *p = 0;
            p++;
            i++;
        } else {
            i++;
            break;
        }
    }
    return i;
}

void main()
{
    char buf[64];
    char line[128];
    char *fields[8];
    int n;

    safe_strcpy(buf, "hello", 64);
    printf("cpy %s\n", buf);

    safe_strcpy(line, "a;b;c;d", 128);
    n = split_csv(line, fields, 8);
    printf("n %d\n", n);
    printf("f0 %s\n", fields[0]);
    printf("f1 %s\n", fields[1]);
    printf("f2 %s\n", fields[2]);
    printf("f3 %s\n", fields[3]);
    printf("done\n");
}
