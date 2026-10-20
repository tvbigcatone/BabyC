/* Minimal getopt / getopt_long for PicoC */
#ifndef PICOC_GETOPT_H
#define PICOC_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#ifndef no_argument
#define no_argument         0
#define required_argument   1
#define optional_argument   2
#endif

#ifndef GETOPT_HAVE_SYSTEM
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int argc, char * const argv[], const char *optstring);
#endif

/* Always provide getopt_long (GNU extension; not always in unistd on every platform) */
int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif
