/* BUG IR (W6 e W7): nei cicli kernel gli elementi di array da 1/2/4 byte sono letti con segno qualunque sia il tipo (ir_fast.c ~1013: signed char / short / int).
   Atteso 1364 (checksum su unsigned char), BabyC 4294966612. Vale anche per unsigned short e unsigned int. NO_IR=1 corretto. */
#include <stdio.h>
int main(){ unsigned char b[8]; int i; unsigned int c=0; for(i=0;i<8;i++) b[i]=(unsigned char)(0xA0+i*3); for(i=0;i<8;i++) c=c+b[i]; printf("%u\n",c); return 0; }
