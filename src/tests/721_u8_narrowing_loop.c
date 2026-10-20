/* BUG IR (W2 e W3): unsigned char non viene troncato a 8 bit nel ciclo. gcc/vanilla=191, BabyC=381079231. NO_IR=1 corretto. */
#include <stdio.h>
int main(){ unsigned char c=7; int i; for(i=0;i<20;i++) c=c*3+1; printf("%u\n",(unsigned)c); return 0; }
