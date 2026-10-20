#include <stdio.h>

struct TNode {
    int v;
    struct TNode *left;
    struct TNode *right;
};

int tree_sum(struct TNode *n) {
    if (n == 0)
        return 0;
    return n->v + tree_sum(n->left) + tree_sum(n->right);
}

int tree_height(struct TNode *n) {
    int lh, rh;
    if (n == 0)
        return 0;
    lh = tree_height(n->left);
    rh = tree_height(n->right);
    return 1 + (lh > rh ? lh : rh);
}

int tree_count(struct TNode *n) {
    if (n == 0)
        return 0;
    return 1 + tree_count(n->left) + tree_count(n->right);
}

int main() {
    struct TNode n1, n2, n3, n4, n5;
    /*       1
            / \
           2   3
          / \
         4   5  */
    n1.v = 1; n2.v = 2; n3.v = 3; n4.v = 4; n5.v = 5;
    n1.left = &n2; n1.right = &n3;
    n2.left = &n4; n2.right = &n5;
    n3.left = 0; n3.right = 0;
    n4.left = 0; n4.right = 0;
    n5.left = 0; n5.right = 0;
    printf("%d\n", tree_sum(&n1));
    printf("%d\n", tree_height(&n1));
    printf("%d\n", tree_count(&n1));
    return 0;
}
