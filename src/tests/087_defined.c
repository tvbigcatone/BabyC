#include <stdio.h>

#define FEATURE 1

int main()
{
#if defined(FEATURE)
    printf("feature_yes\n");
#else
    printf("feature_no\n");
#endif

#if defined(MISSING)
    printf("missing_yes\n");
#else
    printf("missing_no\n");
#endif

#if defined(FEATURE) && FEATURE
    printf("feature_on\n");
#else
    printf("feature_off\n");
#endif

    return 0;
}
