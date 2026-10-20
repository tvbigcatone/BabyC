#include <stdio.h>
#include <stdlib.h>
struct Point { int x; int y; };
int dist2(struct Point *p){ return p->x*p->x + p->y*p->y; }
int main(){
  struct Point *pts; void *raw; int i,s;
  pts=(struct Point*)malloc(3*sizeof(struct Point));
  for(i=0;i<3;i++){ pts[i].x=i+1; pts[i].y=i+2; }
  raw=pts; s=0;
  for(i=0;i<3;i++){ struct Point *base=(struct Point*)raw; s+=dist2(&base[i]); }
  printf("%d\n",s); free(pts); return 0;
}
