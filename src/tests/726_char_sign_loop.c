/* BUG IR (W2 e W3): char semplice letto senza segno nei cicli, con segno fuori. gcc=-448, BabyC=1600. signed char OK. Percorsi incoerenti. */
#include <stdio.h>
#include <stdlib.h>
int main(){ char *p; int i,s=0; p=malloc(8*sizeof(char)); for(i=0;i<8;i++) p[i]=(char)200; for(i=0;i<8;i++) s+=(int)p[i]; printf("%d\n",s); free(p); return 0; }
