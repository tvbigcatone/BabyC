/* update_messages.c - versione PicoC-safe
 * - Niente colonna version (rimossa dal DB)
 * - Chiave univoca: fw_messages.id
 * - Logica conflitti con MERGE (basata su var_name, non univoco):
 *     1) esiste riga IDENTICA (merge senza modifiche) → SKIP
 *     2) esiste riga con sole traduzioni MANCANTI nel DB da riempire
 *        (CSV non vuoto dove DB è vuoto, senza conflitti reali) → FILL
 *     3) altrimenti (conflitto reale) → INSERT nuova riga
 *        (NEW se var_name non esisteva, APPEND se esisteva già)
 *   MERGE: una cella vuota nel CSV significa "non toccare il DB".
 * - I segnaposto ".ITA", ".ENG", ".FRA", ".DEU", ".SPA", ".POR", ".NLD",
 *   ".TUR", ".POL" (case-insensitive) sono trattati come celle vuote,
 *   sia nel CSV che nel DB.
 * - log dettagliato su file, sommario su stdout
 * - percorsi configurabili: #define < config.ini < argomenti riga di comando
 * - parsing argomenti completamente manuale (niente getopt)
 * - usa l'API inih esposta da PicoC (ini_load / ini_get)
 * - Tempo di esecuzione in millisecondi
 * - Genera anche un CSV "messagesSelect.csv" (id;var_name) con le chiavi
 *   (new/append/fill/skip). id = fw_messages.id.
 * - Parametro opzionale -u <ctrl_name> (es. "cam-335").
 *   Se presente, viene salvato in session.ctrl_name (per i trigger).
 *   Se il parametro non viene passato, session.ctrl_name è NULL.
 * - La tabella session (una sola riga, id = 1) contiene:
 *     username  -> utente che esegue (da %USERNAME%)
 *     ctrl_name -> centralina in lavorazione (da -u, oppure NULL)
 *   Viene aggiornata subito dopo sqlite3_open, in modo che i trigger
 *   su fw_messages possano leggere ctrl_name e username correnti.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <windows.h>
#include <sqlite3.h>
#include <ini.h>

/* ===================== DEFAULTS ===================== */
#define DEFAULT_DB_PATH     "F:\\0_Messaggi\\messaggi.db"
#define DEFAULT_CSV_PATH    "strings_orig.csv"
#define DEFAULT_LOG_PATH    "update_messages.log"
#define DEFAULT_INI_PATH    "config.ini"
#define DEFAULT_SELECT_PATH "messagesSelect.csv"

#define MAX_PATH  512
#define MAX_LINE  4096
#define MAX_LANG  128
#define MAX_CTRL  128

/* Azioni possibili su una riga */
#define ACTION_NEW    0
#define ACTION_SKIP   1
#define ACTION_UPDATE 2
#define ACTION_APPEND 3

/* Codici lingua nell'ordine del DB / struct Message */
static const char *g_lang_codes[9] = {
    "ita", "eng", "fra", "deu", "spa", "por", "nld", "tur", "pol"
};

static char g_db_path[MAX_PATH];
static char g_csv_path[MAX_PATH];
static char g_log_path[MAX_PATH];
static char g_ini_path[MAX_PATH];
static char g_select_path[MAX_PATH];   /* output messagesSelect.csv */
static char g_ctrl_name[MAX_CTRL];     /* vuoto = parametro -u non passato */

/* ===================== ANSI COLORS ===================== */
static void enableANSI(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}

#define COL_RED    "\033[31m"
#define COL_GREEN  "\033[32m"
#define COL_RESET  "\033[0m"

typedef struct {
    char var_name[128];
    char ita[MAX_LANG];
    char eng[MAX_LANG];
    char fra[MAX_LANG];
    char deu[MAX_LANG];
    char spa[MAX_LANG];
    char por[MAX_LANG];
    char nld[MAX_LANG];
    char tur[MAX_LANG];
    char pol[MAX_LANG];
} Message;

/* ===================== UTILS ===================== */
static void safe_strcpy(char *dst, const char *src, int dst_size)
{
    int i;
    if (dst == NULL || dst_size <= 0)
        return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i < dst_size - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void trim_newline(char *s)
{
    int n;
    if (s == NULL)
        return;
    n = (int)strlen(s);
    if (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[n - 1] = '\0';
    n = (int)strlen(s);
    if (n > 0 && s[n - 1] == '\r')
        s[n - 1] = '\0';
}

static int split_csv(char *line, char **out, int max)
{
    int i;
    char *p;

    for (i = 0; i < max; i++)
        out[i] = NULL;

    i = 0;
    p = line;

    while (i < max) {
        out[i] = p;

        while (*p != '\0' && *p != ';')
            p++;

        if (*p == ';') {
            *p = '\0';
            p++;
            i++;
        } else {
            i++;
            break;
        }
    }
    return i;
}

/* Se s è esattamente ".XXX" (dove XXX = codice lingua, case-insensitive),
 * lo azzera come se fosse una cella vuota. */
static void normalize_placeholder(char *s, const char *lang_code)
{
    char ph[16];
    int i;
    int len;
    int match;

    if (s == NULL || lang_code == NULL)
        return;
    len = (int)strlen(s);
    if (len < 2 || len > 12)
        return;

    ph[0] = '.';
    for (i = 0; i < 7 && lang_code[i] != '\0'; i++)
        ph[i + 1] = (char)toupper((unsigned char)lang_code[i]);
    ph[i + 1] = '\0';

    if ((int)strlen(ph) != len)
        return;

    match = 1;
    for (i = 0; s[i] != '\0'; i++) {
        if (toupper((unsigned char)s[i]) != (unsigned char)ph[i]) {
            match = 0;
            break;
        }
    }
    if (match)
        s[0] = '\0';
}

/* ===================== DB ===================== */
/* Analizza tutte le righe con stesso var_name e decide l'azione (MERGE):
 *
 *   Per ogni cella lingua confronta CSV (n) con DB (d), entrambi già
 *   normalizzati (placeholder e celle vuote = ""):
 *     - n == ""                       → merge: nessuna modifica (keep DB)
 *     - n != ""  e  n == d            → match
 *     - n != ""  e  d == ""           → fill (DB da riempire)
 *     - n != ""  e  d != ""  e diverso → conflitto reale
 *
 *   Riga compatibile con SKIP  : nessun fill, nessun conflitto
 *   Riga compatibile con FILL  : solo fill (nessun conflitto reale)
 *   Altrimenti (conflitto)     : non compatibile → INSERT nuova riga
 *
 * Priorità risultato: SKIP > FILL > INSERT (NEW/APPEND)
 *
 * out_id: fw_messages.id della riga selezionata (SKIP/FILL).
 */
static int find_action(sqlite3 *db, Message *m, int *out_id)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT id, ita, eng, fra, deu, spa, por, nld, tur, pol "
        "FROM fw_messages WHERE var_name = ?";
    int i;
    const char *new_val[9];
    int found_any = 0;
    int have_identical = 0;
    int ident_id = -1;
    int best_fill_id = -1;

    if (out_id) *out_id = -1;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt) != SQLITE_OK || stmt == NULL)
        return ACTION_NEW;

    sqlite3_bind_text(stmt, 1, m->var_name, -1);

    new_val[0] = m->ita; new_val[1] = m->eng; new_val[2] = m->fra;
    new_val[3] = m->deu; new_val[4] = m->spa; new_val[5] = m->por;
    new_val[6] = m->nld; new_val[7] = m->tur; new_val[8] = m->pol;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int has_real_conflict = 0;
        int has_fillable      = 0;
        char dbuf[MAX_LANG];
        const char *d;
        const char *n;

        found_any = 1;

        for (i = 0; i < 9; i++) {
            const char *p = (const char *)sqlite3_column_text(stmt, i + 1);
            safe_strcpy(dbuf, p ? p : "", MAX_LANG);
            normalize_placeholder(dbuf, g_lang_codes[i]);

            d = dbuf;
            n = new_val[i] ? new_val[i] : "";

            if (n[0] == '\0')
                continue;                /* merge: non tocca il DB */

            if (strcmp(d, n) == 0)
                continue;                /* match */

            if (d[0] == '\0')
                has_fillable = 1;        /* DB vuoto + CSV pieno → fill */
            else
                has_real_conflict = 1;   /* entrambi pieni e diversi */
        }

        if (!has_real_conflict && !has_fillable) {
            have_identical = 1;
            ident_id = id;
        } else if (!has_real_conflict && has_fillable) {
            if (best_fill_id < 0 || id > best_fill_id)
                best_fill_id = id;
        }
    }

    sqlite3_finalize(stmt);

    if (have_identical) {
        if (out_id) *out_id = ident_id;
        return ACTION_SKIP;
    }

    if (best_fill_id >= 0) {
        if (out_id) *out_id = best_fill_id;
        return ACTION_UPDATE;
    }

    if (found_any)
        return ACTION_APPEND;

    return ACTION_NEW;
}

/* Inserisce un nuovo messaggio in fw_messages.
 * out_id: opzionale, restituisce fw_messages.id della riga inserita. */
static int insert_message(sqlite3 *db, Message *m, FILE *log, int *out_id)
{
    sqlite3_stmt *stmt;
    const char *sql;
    int rc;

    if (out_id) *out_id = -1;

    sql = "INSERT INTO fw_messages (var_name, ita, eng, fra, deu, spa, por, nld, tur, pol) "
          "VALUES (?,?,?,?,?,?,?,?,?,?)";
    stmt = 0;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt);
    if (rc != SQLITE_OK || stmt == 0) {
        fprintf(log, "[SQL][PREPARE ERROR] %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, m->var_name, -1);
    sqlite3_bind_text(stmt, 2, m->ita, -1);
    sqlite3_bind_text(stmt, 3, m->eng, -1);
    sqlite3_bind_text(stmt, 4, m->fra, -1);
    sqlite3_bind_text(stmt, 5, m->deu, -1);
    sqlite3_bind_text(stmt, 6, m->spa, -1);
    sqlite3_bind_text(stmt, 7, m->por, -1);
    sqlite3_bind_text(stmt, 8, m->nld, -1);
    sqlite3_bind_text(stmt, 9, m->tur, -1);
    sqlite3_bind_text(stmt, 10, m->pol, -1);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(log, "[SQL][STEP ERROR] %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }
    if (out_id)
        *out_id = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return 1;
}

/* Riempi SOLO le celle vuote (NULL o '') della riga esistente,
 * usando i valori non vuoti del nuovo payload.
 * Non sovrascrive mai valori già presenti.
 * Nota: i placeholder nel DB sono già stati normalizzati a "" in
 * find_action, ma un UPDATE ... WHERE col IS NULL OR col = ''
 * non li intercetterebbe se fossero letteralmente ".NLD"; per questo
 * aggiorniamo anche le celle che contengono un placeholder. */
static int update_fill(sqlite3 *db, int id, Message *m, FILE *log)
{
    sqlite3_stmt *stmt;
    const char *sql;
    int rc;

    sql = "UPDATE fw_messages SET "
          "  ita = CASE WHEN ita IS NULL OR ita = '' OR UPPER(ita) = '.ITA' THEN ? ELSE ita END, "
          "  eng = CASE WHEN eng IS NULL OR eng = '' OR UPPER(eng) = '.ENG' THEN ? ELSE eng END, "
          "  fra = CASE WHEN fra IS NULL OR fra = '' OR UPPER(fra) = '.FRA' THEN ? ELSE fra END, "
          "  deu = CASE WHEN deu IS NULL OR deu = '' OR UPPER(deu) = '.DEU' THEN ? ELSE deu END, "
          "  spa = CASE WHEN spa IS NULL OR spa = '' OR UPPER(spa) = '.SPA' THEN ? ELSE spa END, "
          "  por = CASE WHEN por IS NULL OR por = '' OR UPPER(por) = '.POR' THEN ? ELSE por END, "
          "  nld = CASE WHEN nld IS NULL OR nld = '' OR UPPER(nld) = '.NLD' THEN ? ELSE nld END, "
          "  tur = CASE WHEN tur IS NULL OR tur = '' OR UPPER(tur) = '.TUR' THEN ? ELSE tur END, "
          "  pol = CASE WHEN pol IS NULL OR pol = '' OR UPPER(pol) = '.POL' THEN ? ELSE pol END "
          "WHERE id = ?";

    stmt = 0;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt);
    if (rc != SQLITE_OK || stmt == 0) {
        fprintf(log, "[SQL][PREPARE ERROR] %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, m->ita, -1);
    sqlite3_bind_text(stmt, 2, m->eng, -1);
    sqlite3_bind_text(stmt, 3, m->fra, -1);
    sqlite3_bind_text(stmt, 4, m->deu, -1);
    sqlite3_bind_text(stmt, 5, m->spa, -1);
    sqlite3_bind_text(stmt, 6, m->por, -1);
    sqlite3_bind_text(stmt, 7, m->nld, -1);
    sqlite3_bind_text(stmt, 8, m->tur, -1);
    sqlite3_bind_text(stmt, 9, m->pol, -1);
    sqlite3_bind_int(stmt, 10, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(log, "[SQL][STEP ERROR] %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return 1;
}

/* ===================== SESSION ===================== */
/* Aggiorna la riga unica di session con username e ctrl_name correnti.
 * - username vuoto/NULL  → "unknown"
 * - ctrl_name vuoto/NULL → NULL (nessuna centralina)
 * Da chiamare una volta sola, subito dopo sqlite3_open e PRIMA di
 * qualunque scrittura su fw_messages, così i trigger vedono i valori
 * corretti. */
static int set_session(sqlite3 *db, const char *username,
                       const char *ctrl_name, FILE *log)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO session (id, username, ctrl_name) VALUES (1, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "    username  = excluded.username, "
        "    ctrl_name = excluded.ctrl_name";
    int rc;

    if (username == NULL || username[0] == '\0')
        username = "unknown";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        fprintf(log, "[SQL][PREPARE ERROR session] %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    if (ctrl_name != NULL && ctrl_name[0] != '\0')
        sqlite3_bind_text(stmt, 2, ctrl_name, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 2);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(log, "[SQL][STEP ERROR session] %s\n", sqlite3_errmsg(db));
        return 0;
    }
    return 1;
}

static int reset_session(sqlite3 *db, FILE *log)
{
    return set_session(db, "unknown", "", log);
}

/* ===================== PARSER CSV ===================== */
static void trim_quotes(char *s)
{
    int len;
    int i;

    if (s == NULL)
        return;

    len = (int)strlen(s);

    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        for (i = 0; i < len - 1; i++)
            s[i] = s[i + 1];
        s[len - 2] = '\0';
    }
}

static int parse_line(char *line, Message *m)
{
    char *f[32];
    int n;

    n = split_csv(line, f, 32);
    if (n < 2)
        return 0;

    safe_strcpy(m->var_name, f[0], 128);
    safe_strcpy(m->ita,      f[1], MAX_LANG);
    safe_strcpy(m->eng,      f[2], MAX_LANG);
    safe_strcpy(m->fra,      f[3], MAX_LANG);
    safe_strcpy(m->deu,      f[4], MAX_LANG);
    safe_strcpy(m->spa,      f[5], MAX_LANG);
    safe_strcpy(m->por,      f[6], MAX_LANG);
    safe_strcpy(m->nld,      f[7], MAX_LANG);
    safe_strcpy(m->tur,      f[8], MAX_LANG);
    safe_strcpy(m->pol,      f[9], MAX_LANG);

    trim_quotes(m->var_name);
    trim_quotes(m->ita);
    trim_quotes(m->eng);
    trim_quotes(m->fra);
    trim_quotes(m->deu);
    trim_quotes(m->spa);
    trim_quotes(m->por);
    trim_quotes(m->nld);
    trim_quotes(m->tur);
    trim_quotes(m->pol);

    /* placeholder → cella vuota */
    normalize_placeholder(m->ita, "ita");
    normalize_placeholder(m->eng, "eng");
    normalize_placeholder(m->fra, "fra");
    normalize_placeholder(m->deu, "deu");
    normalize_placeholder(m->spa, "spa");
    normalize_placeholder(m->por, "por");
    normalize_placeholder(m->nld, "nld");
    normalize_placeholder(m->tur, "tur");
    normalize_placeholder(m->pol, "pol");

    return 1;
}

/* ===================== HELP ===================== */
static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <path>   Database path\n");
    printf("  -c <path>   CSV input path\n");
    printf("  -l <path>   Log file path\n");
    printf("  -s <path>   messagesSelect.csv output path (id;var_name)\n");
    printf("  -i <path>   Config INI file (default: config.ini)\n");
    printf("  -u <ctrl>   Control unit name (e.g. cam-335).\n");
    printf("              If specified, stores it in session.ctrl_name so\n");
    printf("              triggers can log it. If omitted, session.ctrl_name\n");
    printf("              is set to NULL.\n");
    printf("  -h          Show this help\n");
    printf("\nPriority: command line > config.ini > compiled defaults\n");
}

/* ===================== PARSER ARGOMENTI ===================== */
static int parse_args(int argc, char **argv)
{
    int i = 1;

    while (i < argc) {
        char *arg = argv[i];

        if (arg[0] != '-') {
            printf("Unknown argument: %s\n", arg);
            return 1;
        }

        if (arg[1] == 'h' && arg[2] == '\0') {
            print_usage(argv[0]);
            return 2;
        }

        if ((arg[1] == 'd' || arg[1] == 'c' || arg[1] == 'l' ||
             arg[1] == 'i' || arg[1] == 's' || arg[1] == 'u') &&
            arg[2] == '\0')
        {
            if (i + 1 >= argc) {
                printf("Option -%c requires an argument\n", arg[1]);
                return 1;
            }
            switch (arg[1]) {
            case 'd': safe_strcpy(g_db_path,     argv[i+1], MAX_PATH); break;
            case 'c': safe_strcpy(g_csv_path,    argv[i+1], MAX_PATH); break;
            case 'l': safe_strcpy(g_log_path,    argv[i+1], MAX_PATH); break;
            case 'i': safe_strcpy(g_ini_path,    argv[i+1], MAX_PATH); break;
            case 's': safe_strcpy(g_select_path, argv[i+1], MAX_PATH); break;
            case 'u': safe_strcpy(g_ctrl_name,   argv[i+1], MAX_CTRL); break;
            }
            i += 2;
            continue;
        }

        if (arg[1] == 'd' || arg[1] == 'c' || arg[1] == 'l' ||
            arg[1] == 'i' || arg[1] == 's' || arg[1] == 'u') {
            switch (arg[1]) {
            case 'd': safe_strcpy(g_db_path,     arg+2, MAX_PATH); break;
            case 'c': safe_strcpy(g_csv_path,    arg+2, MAX_PATH); break;
            case 'l': safe_strcpy(g_log_path,    arg+2, MAX_PATH); break;
            case 'i': safe_strcpy(g_ini_path,    arg+2, MAX_PATH); break;
            case 's': safe_strcpy(g_select_path, arg+2, MAX_PATH); break;
            case 'u': safe_strcpy(g_ctrl_name,   arg+2, MAX_CTRL); break;
            }
            i++;
            continue;
        }

        printf("Unknown option: %s\n", arg);
        return 1;
    }
    return 0;
}

/* ===================== MAIN ===================== */
int main(int argc, char *argv[])
{
    sqlite3 *db = NULL;
    FILE *f = NULL;
    FILE *log = NULL;
    FILE *select_out = NULL;
    char line[MAX_LINE];
    Message m;
    time_t now;
    struct tm *tm_info;
    char timestr[64];
    char *val;
    int i;
    clock_t t_start, t_end;
    long elapsed_ms;

    int cnt_new    = 0;
    int cnt_update = 0;   /* APPEND (nuova riga per var_name già esistente) */
    int cnt_fill   = 0;
    int cnt_skip   = 0;
    int cnt_error  = 0;
    int cnt_total  = 0;
    int rc_exit    = 0;
    int cnt_select = 0;

    /* ---- 1. Default ---- */
    safe_strcpy(g_db_path,     DEFAULT_DB_PATH,     MAX_PATH);
    safe_strcpy(g_csv_path,    DEFAULT_CSV_PATH,    MAX_PATH);
    safe_strcpy(g_log_path,    DEFAULT_LOG_PATH,    MAX_PATH);
    safe_strcpy(g_ini_path,    DEFAULT_INI_PATH,    MAX_PATH);
    safe_strcpy(g_select_path, DEFAULT_SELECT_PATH, MAX_PATH);
    g_ctrl_name[0] = '\0';

    /* ---- 2. Prima passata: solo -i ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            safe_strcpy(g_ini_path, argv[i+1], MAX_PATH);
            break;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'i' && argv[i][2] != '\0') {
            safe_strcpy(g_ini_path, argv[i]+2, MAX_PATH);
            break;
        }
    }

    /* ---- 3. Carica config.ini ---- */
    if (ini_load(g_ini_path) == 0) {
        val = ini_get("paths", "db");
        if (val != NULL) safe_strcpy(g_db_path, val, MAX_PATH);
        val = ini_get("paths", "csv");
        if (val != NULL) safe_strcpy(g_csv_path, val, MAX_PATH);
        val = ini_get("paths", "log");
        if (val != NULL) safe_strcpy(g_log_path, val, MAX_PATH);
        val = ini_get("paths", "select");
        if (val != NULL) safe_strcpy(g_select_path, val, MAX_PATH);
    }

    /* ---- 4. Parsing argomenti ---- */
    {
        int rc = parse_args(argc, argv);
        if (rc == 2) return 0;
        if (rc != 0) return 1;
    }

    t_start = clock();

    log = fopen(g_log_path, "a");
    if (log == NULL) {
        printf("ERRORE: impossibile aprire il file di log: %s\n", g_log_path);
        return 1;
    }

    now = time(NULL);
    tm_info = localtime(&now);
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log, "\n============================================================\n");
    fprintf(log, "UPDATE START  %s\n", timestr);
    fprintf(log, "INI    : %s\n", g_ini_path);
    fprintf(log, "DB     : %s\n", g_db_path);
    fprintf(log, "CSV    : %s\n", g_csv_path);
    fprintf(log, "LOG    : %s\n", g_log_path);
    fprintf(log, "SELECT : %s\n", g_select_path);
    fprintf(log, "CTRL   : %s\n", g_ctrl_name[0] ? g_ctrl_name : "(nessuna centralina)");
    fprintf(log, "============================================================\n");

    if (sqlite3_open(g_db_path, &db) != SQLITE_OK) {
        fprintf(log, "[FATAL] DB open error: %s\n", sqlite3_errmsg(db));
        printf("ERRORE: impossibile aprire il database\n");
        fclose(log);
        return 1;
    }

    /* Abilita le foreign key per questa connessione */
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    /* Imposta session (username + ctrl_name) PRIMA di qualunque scrittura
     * su fw_messages, così i trigger la leggono correttamente. */
    {
        char user[64];
        const char *env = getenv("USERNAME");
        if (env == NULL || env[0] == '\0') env = getenv("USER");
        if (env == NULL || env[0] == '\0') env = "unknown";
        safe_strcpy(user, env, sizeof(user));

        set_session(db, user, g_ctrl_name, log);
        fprintf(log, "[SESSION] username=%s ctrl=%s\n",
                user, g_ctrl_name[0] ? g_ctrl_name : "(null)");
    }

    /* Apre il CSV di output messagesSelect.csv (id;var_name) */
    select_out = fopen(g_select_path, "w");
    if (select_out == NULL) {
        fprintf(log, "[FATAL] cannot open select CSV for write: %s\n", g_select_path);
        printf("ERRORE: impossibile aprire il file di output: %s\n", g_select_path);
        sqlite3_close(db);
        fclose(log);
        return 1;
    }
    fprintf(select_out, "id;var_name\n");

    f = fopen(g_csv_path, "r");
    if (f == NULL) {
        fprintf(log, "[FATAL] CSV not found: %s\n", g_csv_path);
        printf("ERRORE: CSV non trovato\n");
        fclose(select_out);
        sqlite3_close(db);
        fclose(log);
        return 1;
    }

    /* Skip Header */
    fgets(line, sizeof(line), f);

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    while (fgets(line, sizeof(line), f) != NULL) {
        trim_newline(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        memset(&m, 0, sizeof(m));
        if (parse_line(line, &m) == 0)
            continue;

        cnt_total++;

        {
            int existing_id = -1;
            int action = find_action(db, &m, &existing_id);

            if (action == ACTION_SKIP) {
                fprintf(log, "[SKIP] %s id=%d (merge: nessuna modifica)\n",
                        m.var_name, existing_id);
                cnt_skip++;
                if (existing_id >= 0) {
                    fprintf(select_out, "%d;%s\n", existing_id, m.var_name);
                    cnt_select++;
                }
            }
            else if (action == ACTION_UPDATE) {
                if (update_fill(db, existing_id, &m, log)) {
                    fprintf(log, "[FILL] %s id=%d (trad. mancanti riempite)\n",
                            m.var_name, existing_id);
                    cnt_fill++;
                    if (existing_id >= 0) {
                        fprintf(select_out, "%d;%s\n", existing_id, m.var_name);
                        cnt_select++;
                    }
                } else {
                    fprintf(log, "[ERROR] fill failed for %s\n", m.var_name);
                    cnt_error++;
                    rc_exit = 1;
                }
            }
            else {
                int new_id = -1;

                if (insert_message(db, &m, log, &new_id)) {
                    if (action == ACTION_APPEND) {
                        fprintf(log, "[APPEND] %s id=%d (conflitto payload)\n",
                                m.var_name, new_id);
                        cnt_update++;
                    } else {
                        fprintf(log, "[NEW]    %s id=%d\n",
                                m.var_name, new_id);
                        cnt_new++;
                    }
                    fprintf(select_out, "%d;%s\n", new_id, m.var_name);
                    cnt_select++;
                } else {
                    fprintf(log, "[ERROR] insert failed for %s\n", m.var_name);
                    cnt_error++;
                    rc_exit = 1;
                }
            }
        }
    }

    reset_session(db, log);

    fclose(f);
    fclose(select_out);

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_close(db);

    t_end = clock();
    elapsed_ms = (long)((t_end - t_start) * 1000 / CLOCKS_PER_SEC);

    fprintf(log, "------------------------------------------------------------\n");
    fprintf(log, "SUMMARY  total=%d  new=%d  append=%d  fill=%d  skip=%d  error=%d  select=%d\n",
            cnt_total, cnt_new, cnt_update, cnt_fill, cnt_skip, cnt_error, cnt_select);
    fprintf(log, "CTRL     %s\n", g_ctrl_name[0] ? g_ctrl_name : "(nessuna centralina)");
    fprintf(log, "TIME     %ld ms\n", elapsed_ms);
    fprintf(log, "UPDATE END    %s\n", timestr);
    fprintf(log, "============================================================\n");
    fclose(log);

    enableANSI();

    printf("============================================================\n");
    printf("  UPDATE MESSAGES - RISULTATO\n");
    printf("============================================================\n");
    printf("  Config INI       : %s\n", g_ini_path);
    printf("  CSV processato   : %s\n", g_csv_path);
    printf("  Database         : %s\n", g_db_path);
    printf("  Log dettagliato  : %s\n", g_log_path);
    printf("  Select CSV       : %s\n", g_select_path);
    printf("  Centralina       : %s\n",
           g_ctrl_name[0] ? g_ctrl_name : "(nessuna)");
    printf("------------------------------------------------------------\n");
    printf("  Righe elaborate  : %d\n", cnt_total);
    printf("  Nuovi            : %d\n", cnt_new);
    printf("  Nuove versioni   : %d\n", cnt_update);   /* APPEND */
    printf("  Riempimenti      : %d\n", cnt_fill);
    printf("  Saltati (SKIP)   : %d\n", cnt_skip);
    printf("  Errori           : %d\n", cnt_error);
    printf("  Voci in SELECT   : %d\n", cnt_select);
    printf("  Tempo esecuzione : %ld ms\n", elapsed_ms);
    printf("------------------------------------------------------------\n");
    if (cnt_error == 0)
        printf("  Esito            : " COL_GREEN "OK" COL_RESET "\n");
    else
        printf("  Esito            : " COL_RED "ERRORE (vedi log)" COL_RESET "\n");
    printf("============================================================\n");

    return rc_exit;
}