/* PicoC binding for ezxml
 *
 * #include <ezxml.h>
 *
 * High-level (read-only convenience, global document):
 *   int   xml_load(char *filename);           // 0 = ok, <0 error
 *   int   xml_load_string(char *str);
 *   void  xml_clear(void);
 *   char *xml_get(char *path);                // malloc copy of text (or "")
 *   char *xml_attr(char *path, char *name);   // malloc copy of attribute
 *   char *xml_get_string(char *path, char *def);
 *   int   xml_get_int(char *path, int def);
 *   int   xml_ok(void);                       // 1 if a document is loaded
 *   char *xml_error(void);
 *
 * Native API (for build/write as in update_messages.c):
 *   typedef struct ezxml *ezxml_t;  /* real struct so ->next etc. work
 *   ezxml_t ezxml_new(char *name);
 *   ezxml_t ezxml_add_child(ezxml_t xml, char *name, int off);
 *   ezxml_t ezxml_set_attr(ezxml_t xml, char *name, char *value);
 *   ezxml_t ezxml_set_attr_d(ezxml_t xml, char *name, char *value);  // strdup name+value
 *   char   *ezxml_toxml(ezxml_t xml);         // must free() result
 *   void    ezxml_free(ezxml_t xml);
 *   ezxml_t ezxml_set_txt(ezxml_t xml, char *txt);
 *   ezxml_t ezxml_child(ezxml_t xml, char *name);
 *   char   *ezxml_attr(ezxml_t xml, char *attr);  // do not free; owned by tree
 *   char   *ezxml_txt(ezxml_t xml);               // do not free
 *   char   *ezxml_name(ezxml_t xml);              // do not free
 *   ezxml_t ezxml_next(ezxml_t xml);              // same-name sibling
 *   const char *ezxml_error(ezxml_t xml);
 *
 * Path for high-level: "server/host" walks child elements.
 * Strings from xml_get / xml_attr are independent of xml_clear(); free with free() if needed.
 */
#include "../../interpreter.h"
#include "ezxml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static ezxml_t g_root = NULL;

static char *xstrdup(const char *s)
{
    size_t n;
    char *p;
    if (!s) s = "";
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Walk "a/b/c" from root. Returns NULL if any segment missing. */
static ezxml_t walk_path(const char *path)
{
    char buf[512];
    char *p, *seg;
    ezxml_t cur = g_root;

    if (!g_root || !path || !path[0])
        return g_root;

    if (strlen(path) >= sizeof(buf))
        return NULL;
    strcpy(buf, path);

    cur = g_root;
    seg = buf;
    for (p = buf; ; p++) {
        if (*p == '/' || *p == '\0') {
            int end = (*p == '\0');
            *p = '\0';
            if (seg[0] != '\0') {
                cur = ezxml_child(cur, seg);
                if (!cur)
                    return NULL;
            }
            if (end)
                break;
            seg = p + 1;
        }
    }
    return cur;
}

/* ===================== High-level (global document) ===================== */

void Xml_clear(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser; (void)ReturnValue; (void)Param; (void)NumArgs;
    if (g_root) {
        ezxml_free(g_root);
        g_root = NULL;
    }
}

void Xml_load(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *filename = (const char *)Param[0]->Val->Pointer;
    if (g_root) {
        ezxml_free(g_root);
        g_root = NULL;
    }
    if (!filename) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    g_root = ezxml_parse_file(filename);
    if (!g_root) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    if (ezxml_error(g_root) && ezxml_error(g_root)[0]) {
        ReturnValue->Val->Integer = -2;
        return;
    }
    ReturnValue->Val->Integer = 0;
}

void Xml_load_string(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    char *str = (char *)Param[0]->Val->Pointer;
    char *buf;
    size_t len;
    if (g_root) {
        ezxml_free(g_root);
        g_root = NULL;
    }
    if (!str) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    len = strlen(str);
    buf = (char *)malloc(len + 1);
    if (!buf) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    memcpy(buf, str, len + 1);
    /* ezxml_parse_str modifies the buffer in place */
    g_root = ezxml_parse_str(buf, len);
    if (!g_root) {
        free(buf);
        ReturnValue->Val->Integer = -1;
        return;
    }
    /* Tell ezxml_free to free our malloc'd buffer (root->len == -1) */
    {
        struct { struct ezxml xml; void *cur; char *m; size_t len; } *r =
            (void *)g_root;
        r->len = (size_t)-1;
        r->m = buf;
    }
    if (ezxml_error(g_root) && ezxml_error(g_root)[0]) {
        ReturnValue->Val->Integer = -2;
        return;
    }
    ReturnValue->Val->Integer = 0;
}

void Xml_get(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *path = (const char *)Param[0]->Val->Pointer;
    ezxml_t node = walk_path(path);
    const char *txt = node ? ezxml_txt(node) : "";
    ReturnValue->Val->Pointer = xstrdup(txt);
}

void Xml_attr(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *path = (const char *)Param[0]->Val->Pointer;
    const char *name = (const char *)Param[1]->Val->Pointer;
    ezxml_t node = walk_path(path);
    const char *val = (node && name) ? ezxml_attr(node, name) : NULL;
    ReturnValue->Val->Pointer = xstrdup(val ? val : "");
}

void Xml_get_string(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *path = (const char *)Param[0]->Val->Pointer;
    const char *def = (const char *)Param[1]->Val->Pointer;
    ezxml_t node = walk_path(path);
    const char *txt;
    if (!node) {
        ReturnValue->Val->Pointer = xstrdup(def ? def : "");
        return;
    }
    txt = ezxml_txt(node);
    if (!txt || !txt[0]) {
        ReturnValue->Val->Pointer = xstrdup(txt ? txt : "");
        return;
    }
    ReturnValue->Val->Pointer = xstrdup(txt);
}

void Xml_get_int(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *path = (const char *)Param[0]->Val->Pointer;
    int def = Param[1]->Val->Integer;
    ezxml_t node = walk_path(path);
    const char *txt;
    if (!node) {
        ReturnValue->Val->Integer = def;
        return;
    }
    txt = ezxml_txt(node);
    if (!txt || !txt[0]) {
        ReturnValue->Val->Integer = def;
        return;
    }
    ReturnValue->Val->Integer = atoi(txt);
}

void Xml_ok(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser; (void)Param; (void)NumArgs;
    ReturnValue->Val->Integer = (g_root != NULL) ? 1 : 0;
}

void Xml_error(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser; (void)Param; (void)NumArgs;
    if (g_root && ezxml_error(g_root))
        ReturnValue->Val->Pointer = xstrdup(ezxml_error(g_root));
    else
        ReturnValue->Val->Pointer = xstrdup("");
}

/* ===================== Native API (write / build) ===================== */

void Ezxml_new(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *name = (const char *)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    ReturnValue->Val->Pointer = (void *)ezxml_new(name ? name : "root");
}

void Ezxml_add_child(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *name = (const char *)Param[1]->Val->Pointer;
    int off = Param[2]->Val->Integer;
    (void)Parser; (void)NumArgs;
    if (!xml || !name) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    ReturnValue->Val->Pointer = (void *)ezxml_add_child(xml, name, (size_t)off);
}

void Ezxml_set_attr(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *name = (const char *)Param[1]->Val->Pointer;
    const char *value = (const char *)Param[2]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (!xml || !name) {
        ReturnValue->Val->Pointer = (void *)xml;
        return;
    }
    ReturnValue->Val->Pointer = (void *)ezxml_set_attr(xml, name, value);
}

/* Equivalent of ezxml_set_attr_d: strdup name and value, set EZXML_DUP.
 * Safe for temporary buffers (as in update_messages.c). */
void Ezxml_set_attr_d(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *name = (const char *)Param[1]->Val->Pointer;
    const char *value = (const char *)Param[2]->Val->Pointer;
    char *dn, *dv;
    (void)Parser; (void)NumArgs;
    if (!xml || !name) {
        ReturnValue->Val->Pointer = (void *)xml;
        return;
    }
    dn = xstrdup(name);
    dv = xstrdup(value ? value : "");
    if (!dn || !dv) {
        free(dn);
        free(dv);
        ReturnValue->Val->Pointer = (void *)xml;
        return;
    }
    /* Match ezxml_set_attr_d macro behaviour */
    ReturnValue->Val->Pointer = (void *)ezxml_set_attr(
        ezxml_set_flag(xml, EZXML_DUP), dn, dv);
}

void Ezxml_toxml(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (!xml) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    /* ezxml_toxml returns malloc'd string; caller must free() */
    ReturnValue->Val->Pointer = (void *)ezxml_toxml(xml);
}

void Ezxml_free(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    (void)Parser; (void)ReturnValue; (void)NumArgs;
    if (xml) {
        /* If this is the global root, clear it so xml_ok stays consistent */
        if (xml == g_root)
            g_root = NULL;
        ezxml_free(xml);
    }
}

void Ezxml_set_txt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *txt = (const char *)Param[1]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (!xml) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    ReturnValue->Val->Pointer = (void *)ezxml_set_txt(xml, txt ? txt : "");
}

void Ezxml_child(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *name = (const char *)Param[1]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (!xml || !name) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    ReturnValue->Val->Pointer = (void *)ezxml_child(xml, name);
}

/* Native ezxml_attr: returns pointer owned by the tree (do not free). */
void Ezxml_attr_native(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *attr = (const char *)Param[1]->Val->Pointer;
    const char *val;
    (void)Parser; (void)NumArgs;
    if (!xml || !attr) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    val = ezxml_attr(xml, attr);
    ReturnValue->Val->Pointer = (void *)(val ? val : NULL);
}

void Ezxml_txt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    ReturnValue->Val->Pointer = (void *)(xml ? ezxml_txt(xml) : "");
}

void Ezxml_name(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    ReturnValue->Val->Pointer = (void *)(xml ? ezxml_name(xml) : NULL);
}

void Ezxml_error_native(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    const char *err;
    (void)Parser; (void)NumArgs;
    err = xml ? ezxml_error(xml) : "";
    ReturnValue->Val->Pointer = (void *)(err ? err : "");
}


void Ezxml_next(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ezxml_t xml = (ezxml_t)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    /* Same as ezxml_next macro: next sibling with same name, or NULL */
    ReturnValue->Val->Pointer = (void *)(xml ? xml->next : NULL);
}

/* Type + constants visible to PicoC scripts.
 * ezxml_t is an opaque pointer (same pattern as sqlite3 in this tree). */
const char EzxmlDefs[] = "\
struct ezxml {\n\
    char *name;\n\
    char **attr;\n\
    char *txt;\n\
    void *off;\n\
    struct ezxml *next;\n\
    struct ezxml *sibling;\n\
    struct ezxml *ordered;\n\
    struct ezxml *child;\n\
    struct ezxml *parent;\n\
    short flags;\n\
};\n\
typedef struct ezxml *ezxml_t;\n\
#define EZXML_BUFSIZE 1024\n\
#define EZXML_NAMEM   0x80\n\
#define EZXML_TXTM    0x40\n\
#define EZXML_DUP     0x20\n\
";

struct LibraryFunction EzxmlFunctions[] =
{
    /* High-level global document API */
    { Xml_load,           "int xml_load(char *);" },
    { Xml_load_string,    "int xml_load_string(char *);" },
    { Xml_clear,          "void xml_clear();" },
    { Xml_get,            "char *xml_get(char *);" },
    { Xml_attr,           "char *xml_attr(char *, char *);" },
    { Xml_get_string,     "char *xml_get_string(char *, char *);" },
    { Xml_get_int,        "int xml_get_int(char *, int);" },
    { Xml_ok,             "int xml_ok();" },
    { Xml_error,          "char *xml_error();" },

    /* Native API used by update_messages.c and similar */
    { Ezxml_new,          "ezxml_t ezxml_new(char *);" },
    { Ezxml_add_child,    "ezxml_t ezxml_add_child(ezxml_t, char *, int);" },
    { Ezxml_set_attr,     "ezxml_t ezxml_set_attr(ezxml_t, char *, char *);" },
    { Ezxml_set_attr_d,   "ezxml_t ezxml_set_attr_d(ezxml_t, char *, char *);" },
    { Ezxml_toxml,        "char *ezxml_toxml(ezxml_t);" },
    { Ezxml_free,         "void ezxml_free(ezxml_t);" },
    { Ezxml_set_txt,      "ezxml_t ezxml_set_txt(ezxml_t, char *);" },
    { Ezxml_child,        "ezxml_t ezxml_child(ezxml_t, char *);" },
    { Ezxml_attr_native,  "char *ezxml_attr(ezxml_t, char *);" },
    { Ezxml_txt,          "char *ezxml_txt(ezxml_t);" },
    { Ezxml_name,         "char *ezxml_name(ezxml_t);" },
    { Ezxml_error_native, "char *ezxml_error(ezxml_t);" },
    { Ezxml_next,         "ezxml_t ezxml_next(ezxml_t);" },

    { NULL, NULL }
};

void EzxmlSetupFunc(Picoc *pc)
{
    (void)pc;
}
