/* BUG (vecchia, RISOLTO nella nuova): "struct N *p,*q;" locale perdeva il tipo di q.
   Atteso: exit 5 / output 5. Vecchia: "doesn't have a member called 'v'". Da tenere come test di regressione. */
#include <stdio.h>
struct N { int v; };
int main(){ struct N a; struct N *p,*q; a.v=5; p=&a; q=p; printf("%d\n",q->v); return 0; }
