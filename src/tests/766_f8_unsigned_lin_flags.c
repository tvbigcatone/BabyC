/* F8 structural: TypeFlags UNSIGNED → IrLinBin Imm; EXEC=both match. */
#include <stdio.h>
int main(void)
{
    unsigned int u = 0xFFFFFFF0u;
    unsigned int v = u >> 4;
    unsigned int w = u / 16u;
    int s = (int)0xFFFFFFF0;
    int t = s >> 4;
    printf("%u %u %d\n", v, w, t);
    return 0;
}
