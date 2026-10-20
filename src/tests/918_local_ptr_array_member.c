/* Anti-regression: local T* adj[N]; adj[i]->m / adj[i]->next->m under vm_strict. */
#include <stdio.h>
struct Edge { int to; struct Edge *next; };
struct Edge e01, e02;
int main(void) {
  struct Edge *adj[2];
  e01.to = 1; e01.next = &e02;
  e02.to = 2; e02.next = 0;
  adj[0] = &e01; adj[1] = 0;
  printf("%d %d\n", adj[0]->to, adj[0]->next->to);
  return 0;
}
