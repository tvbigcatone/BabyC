/* Cross: struct fields + p->m + recursion + pointer params */
#include <stdio.h>

struct Node {
    int val;
    int left;
    int right;
};

struct Node pool[8];

int walk(struct Node *n, int idx)
{
    int s;
    if (idx < 0)
        return 0;
    s = n[idx].val;
    s = s + walk(n, n[idx].left);
    s = s + walk(n, n[idx].right);
    return s;
}

int sum_ptr(struct Node *p)
{
    return p->val + (p->left >= 0 ? sum_ptr(&pool[p->left]) : 0)
                  + (p->right >= 0 ? sum_ptr(&pool[p->right]) : 0);
}

int main()
{
    /*     0
          / \
         1   2
        /
       3
    */
    pool[0].val = 10; pool[0].left = 1; pool[0].right = 2;
    pool[1].val = 3;  pool[1].left = 3; pool[1].right = -1;
    pool[2].val = 4;  pool[2].left = -1; pool[2].right = -1;
    pool[3].val = 2;  pool[3].left = -1; pool[3].right = -1;
    printf("%d %d\n", walk(pool, 0), sum_ptr(&pool[0]));
    return 0;
}
