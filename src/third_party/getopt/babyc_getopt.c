/* PicoC binding for getopt / getopt_long
 * #include <getopt.h>
 */
#include "../../interpreter.h"

#ifdef WIN32
# include "getopt.h"
#else
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
# include <getopt.h>   /* system getopt + getopt_long + struct option */
# include <unistd.h>
#endif

/* int getopt(int argc, char **argv, char *optstring); */
void Getopt_getopt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    int argc = Param[0]->Val->Integer;
    char **argv = (char **)Param[1]->Val->Pointer;
    const char *optstring = (const char *)Param[2]->Val->Pointer;

    if (argv == NULL || optstring == NULL) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    ReturnValue->Val->Integer = getopt(argc, argv, optstring);
}

/* int getopt_long(int argc, char **argv, char *optstring,
 *                 struct option *longopts, int *longindex);
 * longindex may be NULL.
 */
void Getopt_getopt_long(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    int argc = Param[0]->Val->Integer;
    char **argv = (char **)Param[1]->Val->Pointer;
    const char *optstring = (const char *)Param[2]->Val->Pointer;
    struct option *longopts = (struct option *)Param[3]->Val->Pointer;
    int *longindex = (int *)Param[4]->Val->Pointer;

    if (argv == NULL) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    ReturnValue->Val->Integer = getopt_long(argc, argv,
                                            optstring ? optstring : "",
                                            longopts, longindex);
}

const char GetoptDefs[] = "\
struct option {\
    char *name;\
    int has_arg;\
    int *flag;\
    int val;\
};\
#define no_argument 0\n\
#define required_argument 1\n\
#define optional_argument 2\n\
";

struct LibraryFunction GetoptFunctions[] =
{
    { Getopt_getopt,      "int getopt(int, char **, char *);" },
    { Getopt_getopt_long, "int getopt_long(int, char **, char *, struct option *, int *);" },
    { NULL, NULL }
};

void GetoptSetupFunc(Picoc *pc)
{
    VariableDefinePlatformVar(pc, NULL, "optarg", pc->CharPtrType, (union AnyValue *)&optarg, TRUE);
    VariableDefinePlatformVar(pc, NULL, "optind", &pc->IntType, (union AnyValue *)&optind, TRUE);
    VariableDefinePlatformVar(pc, NULL, "opterr", &pc->IntType, (union AnyValue *)&opterr, TRUE);
    VariableDefinePlatformVar(pc, NULL, "optopt", &pc->IntType, (union AnyValue *)&optopt, TRUE);
}
