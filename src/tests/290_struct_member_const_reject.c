#include <stdio.h>

/* Phase A safety check: IrbBind must refuse to bind a write when the
 * struct root is const (same check already used for plain scalars and
 * arrays: (Use & IRB_USE_W) && V->IsConst -> bind refused), so this must
 * fall back to the token path and fail with the *same* diagnostic and
 * caret column as without Irb at all -- never a silent const violation,
 * never a different error. */

struct box {
    int x;
};

void main()
{
    const struct box b = {5};
    int i;

    for (i = 0; i < 3; i++)
        b.x = i;

    printf("x %d\n", b.x);
}
