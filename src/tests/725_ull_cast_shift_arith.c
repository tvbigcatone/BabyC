/* BUG IR (W6 e W7): (unsigned long long)a>>59 con a negativo fa shift ARITMETICO. Atteso 31, BabyC -1. NO_IR=1 corretto. Anche fuori dai cicli. */
#include <stdio.h>
int main(){ long long a=-16LL; long long r; r=(long long)((unsigned long long)a>>59); printf("%lld\n",r); return 0; }
