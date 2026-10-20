/* Local struct with array member and array of local structs. */

#include <stdio.h>

int main()
{
    struct Item {
        int id;
        int vals[3];
    };
    struct Item it;
    it.id = 7;
    it.vals[0] = 10;
    it.vals[1] = 20;
    it.vals[2] = 30;
    printf("%d %d\n", it.id, it.vals[0] + it.vals[1] + it.vals[2]);

    struct Item arr[2];
    arr[0].id = 1;
    arr[0].vals[0] = 5;
    arr[1].id = 2;
    arr[1].vals[0] = 8;
    printf("%d %d\n", arr[0].id + arr[1].id, arr[0].vals[0] + arr[1].vals[0]);
    return 0;
}
