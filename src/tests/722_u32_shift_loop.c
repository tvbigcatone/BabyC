/* BUG IR (W2 e W3): unsigned int non troncato a 32 bit prima di >>. gcc/vanilla=5, BabyC=584476853. NO_IR=1 corretto. Stessa causa: (b*b)>>16, xorshift. */
#include <stdio.h>
int main(){ unsigned int b=584476853u; unsigned int r=0; int i; for(i=0;i<1;i++) r=(b<<28)>>28; printf("%u\n",r); return 0; }
