/* B.3: Intrinsic pack from frame + CachedVal — printf/scanf without Get on args.
 * Globals and locals mixed; &f scanf path (B.2) still required.
 */
#include <stdio.h>
int G = 7;
char msg[] = "hi";
int main(void)
{
    int x = 3;
    float f;
    double d;
    int i;
    char buf[32];
    printf("%d %d %s\n", x, G, msg);
    sprintf(buf, "n=%d", x + G);
    printf("%s\n", buf);
    if (sscanf("1.5 2.5 9", "%f %lf %d", &f, &d, &i) != 3)
        return 1;
    printf("f=%.2f d=%.2f i=%d\n", f, d, i);
    return 0;
}
