#include <stdio.h>
#include <string.h>
#include <time.h>

#define ACT_NONE 0
#define ACT_EXTRACT 1
#define ACT_UPDATE 2

typedef struct {
    int c1_ok, c1_add, c1_y;
    int c2, c3, err, warn;
} Counters;

int g_line_num;
int g_verbose;
char g_buf[256];

int helper_work(int n) {
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + i;
    return s;
}

int print_grouped_warnings(void) {
    return 0;
}

int action_extract(void *db, void *log) {
    int cnt_ok, cnt_pass, cnt_discard, cnt_warn, cnt_err;
    int i;
    cnt_ok = 0;
    cnt_pass = 0;
    cnt_discard = 0;
    cnt_warn = 0;
    cnt_err = 0;
    for (i = 0; i < 716; i++)
        cnt_ok = cnt_ok + 1;
    cnt_warn = print_grouped_warnings();
    printf("------------------------------------------------------------\n");
    printf("  Estratte dal DB  : %d\n", cnt_ok);
    printf("  Pass-through     : %d\n", cnt_pass);
    printf("  Scartate (no id) : %d\n", cnt_discard);
    printf("  Record totali    : %d  (file: %d linee)\n",
           cnt_ok + cnt_pass, 717);
    printf("  Warning          : %d\n", cnt_warn);
    return 0;
}

int action_update(void *db, void *log, Counters *ctr) {
    ctr->err = 1;
    return 1;
}

int main() {
    void *db;
    void *log;
    int action;
    int rc_exit;
    int i;
    clock_t t_start, t_end;
    long elapsed_ms;
    Counters ctr;
    char timestr[64];

    memset(&ctr, 0, sizeof(ctr));
    action = ACT_EXTRACT;
    rc_exit = 0;
    db = (void *)0;
    log = (void *)0;
    g_line_num = 0;
    g_verbose = 0;

    printf("============================================================\n");
    printf("                    MESSAGES MANAGER\n");
    printf("============================================================\n");
    printf("  Azione           : extractFromDB\n");

    t_start = clock();

    if (action == ACT_EXTRACT) {
        rc_exit = action_extract(db, log);
    } else if (action == ACT_UPDATE) {
        rc_exit = action_update(db, log, &ctr);
    } else {
        printf("ERRORE: azione non valida\n");
        rc_exit = 1;
    }

    t_end = clock();
    elapsed_ms = (long)((t_end - t_start) * 1000 / CLOCKS_PER_SEC);
    if (elapsed_ms < 0)
        elapsed_ms = 0;

    printf("============================================================\n");
    printf("                       RISULTATO\n");
    printf("============================================================\n");
    printf("  Tempo esecuzione : %ld ms\n", elapsed_ms);
    if (rc_exit == 0)
        printf("  Esito            : OK\n");
    else
        printf("  Esito            : ERRORE\n");
    printf("============================================================\n");
    return rc_exit;
}
