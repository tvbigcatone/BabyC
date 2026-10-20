# BabyPOSIX — Step 17: contratti di errore e limiti dei descriptor

## Obiettivo

Aggiungere copertura di regressione per le principali condizioni di errore delle API descriptor-backed, senza modificare intenzionalmente il comportamento dell'implementazione.

## Nuovo test

`src/tests/posix/fd_errors.c` verifica che:

- `open()` rifiuti flag sconosciuti;
- `read()` e `write()` rifiutino un descriptor negativo;
- `lseek()` rifiuti un `whence` non valido;
- `dup2()` rifiuti target negativi e oltre la capacità della tabella (256 slot);
- `fcntl(F_SETFD)` rifiuti flag descriptor non supportati;
- le operazioni fallite precedenti non rendano inutilizzabile il descriptor sorgente;
- la chiusura di un descriptor già chiuso fallisca;
- `pipe()` continui a funzionare dopo le operazioni non valide.

## Verifica

Il test è pensato per essere eseguito dal runner MSVC già presente:

```bat
run-posix-tests-msvc.bat
```

La verifica runtime su Windows/MSVC deve essere eseguita nell'ambiente dell'utente. Non si dichiara qui un esito MSVC non ancora osservato. L'aggiunta del test non equivale a dimostrare che il runtime imposti tutti i valori `errno` previsti: il test controlla i risultati delle chiamate e la coerenza dello stato.

## Prossimo controllo

Dopo l'esecuzione MSVC, usare gli eventuali messaggi `FAIL` per isolare i contratti non rispettati. Solo dopo un risultato riproducibile intervenire nell'implementazione e aggiungere un test mirato.
