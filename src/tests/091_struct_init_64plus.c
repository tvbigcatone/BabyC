#include <stdio.h>

struct Big {
    int m00, m01, m02, m03, m04, m05, m06, m07, m08, m09;
    int m10, m11, m12, m13, m14, m15, m16, m17, m18, m19;
    int m20, m21, m22, m23, m24, m25, m26, m27, m28, m29;
    int m30, m31, m32, m33, m34, m35, m36, m37, m38, m39;
    int m40, m41, m42, m43, m44, m45, m46, m47, m48, m49;
    int m50, m51, m52, m53, m54, m55, m56, m57, m58, m59;
    int m60, m61, m62, m63;
};

int main()
{
    struct Big b = {
        0,1,2,3,4,5,6,7,8,9,
        10,11,12,13,14,15,16,17,18,19,
        20,21,22,23,24,25,26,27,28,29,
        30,31,32,33,34,35,36,37,38,39,
        40,41,42,43,44,45,46,47,48,49,
        50,51,52,53,54,55,56,57,58,59,
        60,61,62,63
    };

    printf("%d\n", b.m00);
    printf("%d\n", b.m32);
    printf("%d\n", b.m63);

    int sum = 0;
    int i;
    int *p = &b.m00;
    for (i = 0; i < 64; i++)
        sum += p[i];
    printf("%d\n", sum);

    return 0;
}