#include <stdio.h>
int inc(int x){return x+1;}
int dec(int x){return x-1;}
int main(){
  enum Op { INC=0, DEC=1 };
  union Slot { int (*fp)(int); int raw; };
  union Slot u; enum Op op; int v;
  op=INC; u.fp=(op==INC)?&inc:&dec; v=10; v=(*u.fp)(v); printf("%d\n",v);
  op=DEC; u.fp=(op==INC)?&inc:&dec; v=(*u.fp)(v); printf("%d\n",v);
  return 0;
}
