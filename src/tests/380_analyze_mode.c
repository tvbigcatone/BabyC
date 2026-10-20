/* Run with: babyc --analyze 380_analyze_mode.c
 * Must not execute main (no "EXECUTED" line). */
#include <stdio.h>
int f(int x) { return x + 1; }
int main(void)
{
    printf("EXECUTED\n");
    return f(0);
}
