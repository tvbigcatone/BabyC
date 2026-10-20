#include <stdio.h>

typedef float FloatAlias;
typedef FloatAlias FloatTriple[3];
typedef FloatAlias *FloatPointer;
typedef FloatPointer FloatPointerAlias;

struct FloatMembers {
    FloatAlias scalar;
    FloatTriple values;
    FloatPointer pointer;
};

int main(void)
{
    FloatTriple values;
    struct FloatMembers item;
    printf("scalar=%d triple=%d pointer=%d pointer_alias=%d members=%d array_object=%d struct_object=%d\n",
        (int)sizeof(FloatAlias), (int)sizeof(FloatTriple),
        (int)sizeof(FloatPointer), (int)sizeof(FloatPointerAlias),
        (int)sizeof(struct FloatMembers), (int)sizeof(values),
        (int)sizeof(item));
    return 0;
}
