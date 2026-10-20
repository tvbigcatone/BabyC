#include <stdio.h>
int main() {
  int data[4]; int *p0,**pp,***ppp; int i,acc;
  data[0]=1;data[1]=2;data[2]=3;data[3]=4;
  p0=data; pp=&p0; ppp=&pp; acc=0;
  for(i=0;i<4;i++){ acc+=(***ppp>2)?***ppp:(***ppp)*10; p0++; }
  printf("%d\n", acc);
  p0=data;
  for(i=0;i<4;i++){ ***ppp=***ppp+i; p0++; }
  printf("%d %d %d %d\n", data[0],data[1],data[2],data[3]);
  return 0;
}
