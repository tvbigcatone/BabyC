#include <stdio.h>

/* Regression test for ExpressionAssign() / TypeArray case in expression.c.
 *
 * Bug: when assigning an array to an unsized destination array
 * (e.g. "int dst[] = src;"), the guard that should check the element
 * type compatibility was tautological:
 *     DestValue->Typ->FromType == DestValue->Typ->FromType
 * (always true), instead of:
 *     SourceValue->Typ->FromType == DestValue->Typ->FromType
 *
 * As a result an unsized destination array silently adopted the
 * *source's* element type whenever the source was any array, even if
 * the element types did not match (e.g. "int dst[] = a_char_array;"
 * turned dst into a char array instead of failing).
 *
 * This test checks:
 *  1) the legitimate case (matching element types) still works and
 *     resizes/copies correctly;
 *  2) a mismatched-element-type case is now rejected with a clean
 *     assignment error instead of silently changing dst's type.
 */

int main()
{
    int src_ok[3] = {10, 20, 30};
    int dst_ok[] = src_ok;
    printf("ok %d %d %d sizeof=%d\n", dst_ok[0], dst_ok[1], dst_ok[2], (int)sizeof(dst_ok));

    char src_bad[3] = {1, 2, 3};
    int dst_bad[] = src_bad;
    printf("should not be reached\n");

    return 0;
}
