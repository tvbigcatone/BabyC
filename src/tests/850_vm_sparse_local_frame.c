#include <stdio.h>
int main(void)
{
    int spare00, spare01, spare02, spare03, spare04, spare05, spare06, spare07;
    int spare08, spare09, spare10, spare11, spare12, spare13, spare14, spare15;
    int spare16, spare17, spare18, spare19, spare20, spare21, spare22, spare23;
    int spare24, spare25, spare26, spare27, spare28, spare29, spare30, spare31;
    int spare32, spare33, spare34, spare35, spare36, spare37, spare38, spare39;
    int spare40, spare41, spare42, spare43, spare44, spare45, spare46, spare47;
    int spare48, spare49, spare50, spare51, spare52, spare53, spare54, spare55;
    int spare56, spare57, spare58, spare59, spare60, spare61, spare62, spare63;
    int i = 0, sum = 0;
    while (i < 200000) {
        sum += i & 7;
        i++;
    }
    printf("%d %d\n", i, sum);
    return 0;
}
