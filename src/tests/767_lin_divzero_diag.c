/* F8: linear/VM prod must ProgramFail on /0 (same as tree). */
int main(void)
{
    int i, x = 10, z = 0;
    for (i = 0; i < 1; i++)
        x = x / z;
    return x;
}
