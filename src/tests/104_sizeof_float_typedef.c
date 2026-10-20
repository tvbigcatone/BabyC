#include <stdio.h>
typedef float FloatAlias;
typedef const float ConstFloatAlias;
typedef FloatAlias ChainedFloatAlias;
typedef double DoubleAlias;
typedef float *FloatPointerAlias;
int main(void)
{
    printf("float_alias=%d const_float_alias=%d chained_alias=%d double_alias=%d float_pointer_alias=%d\n",
        (int)sizeof(FloatAlias), (int)sizeof(ConstFloatAlias),
        (int)sizeof(ChainedFloatAlias), (int)sizeof(DoubleAlias),
        (int)sizeof(FloatPointerAlias));
    return 0;
}
