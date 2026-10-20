#include <stdio.h>

float char_to_fp(char c)
{
    return c + 0.5f;
}

double mixed_relation(int v)
{
    return v < 12.5 ? 12.5 : v;
}

printf("%f %f\n", char_to_fp('A'), mixed_relation(12));
void main() {}
