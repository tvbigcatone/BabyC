#include <stdio.h>
struct Cell { int v; struct Cell *next; };
int bump(int x){ return x+1; }
int main(){
  struct Cell a,b; struct Cell *p; int (*fp)(int); int t;
  a.v=1; b.v=2; a.next=&b; b.next=NULL; p=&a; fp=&bump;
  t=p->v; t=t+1; p->v=t; printf("%d\n",(*fp)(t)); printf("%d\n",a.v);
  p=a.next; t=p->v; t=t-1; p->v=t; printf("%d\n",(*fp)(t)); printf("%d\n",b.v);
  p=&a; a.v=0; b.v=0;
  while(p!=NULL){ p->v=(*fp)(p->v); p->v=(*fp)(p->v); p=p->next; }
  printf("%d %d\n", a.v, b.v); return 0;
}
