# W28 — sessione ampliamento superficie VM

## Baseline verificata
- Albero sorgente estratto dal pacchetto fornito.
- Build Linux: `make clean && make -j2` completata correttamente.
- Suite completa con `BABYC_EXEC=vm_strict`: 606 pass, 223 fail, 22 skipped (suite locale `tools/run_suite.py`).
- Il conteggio della suite locale non è direttamente confrontabile con il contatore W28 riportato nella richiesta: il runner include casi/aspettative differenti.

## Risultato di questo turno
Nessuna modifica semantica al lowerer è stata applicata in questo turno. L'ispezione ha confermato che i residui includono molti fallback di interi corpi, con concentrazione su function pointer, macro/compatibilità parser e inizializzazioni/array multidimensionali. Senza poter eseguire il gate W28 esatto e verificare i relativi output anti-regressione, cambiare i gate di lowering rischierebbe di aumentare il conteggio apparente sacrificando la correttezza.

## Prossimo passo raccomandato
Eseguire il gate W28 esatto sul pacchetto baseline e raccogliere, per ogni fallimento `vm_strict`, la prima causa di rifiuto del lowerer. Poi ampliare una sola famiglia per volta (prima i casi scalari/array con semantica già coperta), aggiungendo un test anti-regressione per ogni famiglia e verificando `default`, `EXEC=vm` e `vm_strict`.
