#include <stdio.h>

/* adjacency via next-sibling lists */
struct Edge { int to; struct Edge *next; };
struct Edge e01, e02, e12, e13;

int visited[4];
int order[4];
int ocnt;

void dfs(int u, struct Edge **adj) {
    struct Edge *e;
    if (visited[u])
        return;
    visited[u] = 1;
    order[ocnt] = u;
    ocnt = ocnt + 1;
    e = adj[u];
    while (e != 0) {
        dfs(e->to, adj);
        e = e->next;
    }
}

int main() {
    struct Edge *adj[4];
    int i;
    /* 0->1, 0->2, 1->2, 1->3 */
    e01.to = 1; e01.next = &e02;
    e02.to = 2; e02.next = 0;
    e12.to = 2; e12.next = &e13;
    e13.to = 3; e13.next = 0;
    adj[0] = &e01; adj[1] = &e12; adj[2] = 0; adj[3] = 0;
    for (i = 0; i < 4; i = i + 1) visited[i] = 0;
    ocnt = 0;
    dfs(0, adj);
    for (i = 0; i < ocnt; i = i + 1) {
        if (i) printf(" ");
        printf("%d", order[i]);
    }
    printf("\n");
    return 0;
}
