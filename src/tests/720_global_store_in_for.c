/* BUG IR (W6 e W7): in un for con condizione < o <= e corpo semplice, ogni scrittura su una VARIABILE GLOBALE scalare (qualsiasi tipo) viene scartata.
   Atteso 9, BabyC 0. Con while, !=, do/while, chiamata o printf nel corpo e corretto; NO_IR=1 corretto.
   Causa: IrKernelEval, case IrExAssign (ir_fast.c ~1022): se Left e un ident senza LocalSlot ne ParamSlot (globale) nessun ramo scrive il valore. */
#include <stdio.h>
int G=0;
int main(){ int i; for(i=0;i<10;i++){ G=i; } printf("%d\n",G); return 0; }
