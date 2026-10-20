# BabyC — W58: verifica dell'aliasing FP

Delta cumulativo rispetto a `babyc(20261015-071830).zip`. Il repro `open_fp_alias_copy.c` mostra che Tree/default e VM non osservano gli store `float` attraverso puntatore, mentre token sì. La build GCC e i test delle modalità FP (28/28) passano, ma l'audit architetturale FP resta 0/4. Nessuna correzione di backend è stata introdotta perché la causa non era ancora dimostrata.
