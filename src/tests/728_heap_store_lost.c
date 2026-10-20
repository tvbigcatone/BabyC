/* BUG IR (W2 e W3): su malloc fresco, *(p+i)=x in ciclo + secondo ciclo => p[0],p[1] garbage NON deterministico (residui heap). Atteso 75. Con blocco pre-riempito torna corretto. vanilla OK. */
#include <stdio.h>
#include <stdlib.h>
int main(){ int i,s=0; int *p; p=malloc(6*sizeof(int)); for(i=0;i<6;i++) *(p+i)=i+10; for(i=0;i<6;i++) s+=p[i]; printf("%d\n",s); return 0; }
