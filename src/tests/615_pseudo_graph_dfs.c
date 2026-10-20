#include <stdio.h>
#include <stdlib.h>
struct Edge { int to; struct Edge *next; };
struct Graph { struct Edge *adj[8]; int n; int seen[8]; };
void add_edge(struct Graph *g,int u,int v){
  struct Edge *e=(struct Edge*)malloc(sizeof(struct Edge));
  e->to=v; e->next=g->adj[u]; g->adj[u]=e;
}
int dfs(struct Graph *g,int u,int target){
  struct Edge *e;
  if(u==target) return 1;
  if(g->seen[u]) return 0;
  g->seen[u]=1;
  for(e=g->adj[u]; e!=NULL; e=e->next)
    if(dfs(g,e->to,target)) return 1;
  return 0;
}
void free_graph(struct Graph *g){
  int i; for(i=0;i<g->n;i++){ struct Edge *e=g->adj[i];
    while(e!=NULL){ struct Edge *n=e->next; free(e); e=n; } }
}
int main(){
  struct Graph g; int i; g.n=5;
  for(i=0;i<8;i++){ g.adj[i]=NULL; g.seen[i]=0; }
  add_edge(&g,0,1); add_edge(&g,0,2); add_edge(&g,1,3); add_edge(&g,2,3); add_edge(&g,3,4);
  printf("%d\n", dfs(&g,0,4));
  for(i=0;i<8;i++) g.seen[i]=0;
  printf("%d\n", dfs(&g,1,2));
  free_graph(&g); return 0;
}
