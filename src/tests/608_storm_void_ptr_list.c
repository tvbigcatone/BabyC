#include <stdio.h>
#include <stdlib.h>
struct Node { int value; };
int main() {
  struct Node *pool; void *raw; struct Node *again; int i,s;
  pool=(struct Node*)malloc(3*sizeof(struct Node));
  for(i=0;i<3;i++) pool[i].value=i+1;
  raw=pool; again=(struct Node*)raw; s=0;
  for(i=0;i<3;i++) s+=again[i].value;
  printf("%d\n",s); printf("%d\n",again[0].value); printf("%d\n",again[1].value);
  free(pool); return 0;
}
