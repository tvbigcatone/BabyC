#include <stdio.h>

FILE *f = fopen("fred.txt", "wb");
if (f == NULL) {
    printf("fopen write failed\n");
} else {
    fwrite("hello\nhello\n", 1, 12, f);
    fclose(f);
}

char freddy[7];
char *buf = freddy;   /* force array-to-pointer so library sees a real pointer */
f = fopen("fred.txt", "rb");
if (f == NULL) {
    printf("fopen read failed\n");
} else {
    if (fread(buf, 1, 6, f) != 6)
        printf("couldn't read fred.txt\n");

    freddy[6] = '\0';
    fclose(f);

    printf("%s", freddy);

    char InChar;
    char ShowChar;
    f = fopen("fred.txt", "rb");
    while ( (InChar = fgetc(f)) != EOF)
    {
        ShowChar = InChar;
        if (ShowChar < ' ')
            ShowChar = '.';

        printf("ch: %d '%c'\n", InChar, ShowChar);
    }
    fclose(f);

    f = fopen("fred.txt", "rb");
    while ( (InChar = getc(f)) != EOF)
    {
        ShowChar = InChar;
        if (ShowChar < ' ')
            ShowChar = '.';

        printf("ch: %d '%c'\n", InChar, ShowChar);
    }
    fclose(f);

    f = fopen("fred.txt", "rb");
    while (fgets(buf, sizeof(freddy), f) != NULL)
        printf("x: %s", freddy);

    fclose(f);
}

void main() {}
