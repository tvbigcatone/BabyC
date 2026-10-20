/* BUG IR (W2 e W3): (unsigned int)int negativo non azzerato nei 32 bit alti. gcc=2147483645, BabyC=4294967293. */
#include <stdio.h>
int main(){ unsigned int s=0; int i; int v=-5; for(i=0;i<1;i++){ s=(unsigned int)v; s=s>>1; } printf("%u\n",s); return 0; }
