#include <stdio.h>

#define VERSION 3
#define ENABLED 1
#define DISABLED 0

#if defined(VERSION)
    #define HAS_VERSION 1
#else
    #define HAS_VERSION 0
#endif

#if VERSION > 2
    #define NEW_ENOUGH 1
#else
    #define NEW_ENOUGH 0
#endif

#if ENABLED && VERSION
    #define BOTH_ON 1
#else
    #define BOTH_ON 0
#endif

#if ENABLED || DISABLED
    #define AT_LEAST_ONE 1
#else
    #define AT_LEAST_ONE 0
#endif

#if !defined(MISSING)
    #define MISSING_ABSENT 1
#else
    #define MISSING_ABSENT 0
#endif

int main()
{
    printf("has_version=%d\n", HAS_VERSION);
    printf("new_enough=%d\n", NEW_ENOUGH);
    printf("both_on=%d\n", BOTH_ON);
    printf("at_least_one=%d\n", AT_LEAST_ONE);
    printf("missing_absent=%d\n", MISSING_ABSENT);
    return 0;
}