/* Portable getopt + getopt_long for PicoC hosts (esp. Win32).
 * getopt is only compiled if GETOPT_HAVE_SYSTEM is not defined.
 * getopt_long is always provided by this file.
 */
#include "getopt.h"
#include <stdio.h>
#include <string.h>

#ifndef GETOPT_HAVE_SYSTEM
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

static char *nextchar = NULL;

int getopt(int argc, char * const argv[], const char *optstring)
{
    char c;
    const char *cp;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    if (nextchar == NULL || *nextchar == '\0') {
        if (argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }
        nextchar = &argv[optind][1];
    }

    c = *nextchar++;
    optopt = (unsigned char)c;

    if (*nextchar == '\0') {
        nextchar = NULL;
        optind++;
    }

    cp = strchr(optstring, c);
    if (cp == NULL || c == ':') {
        if (opterr)
            fprintf(stderr, "unknown option -- %c\n", c);
        return '?';
    }

    if (cp[1] == ':') {
        if (nextchar != NULL && *nextchar != '\0') {
            optarg = nextchar;
            nextchar = NULL;
            optind++;
        } else if (optind >= argc) {
            if (opterr)
                fprintf(stderr, "option requires an argument -- %c\n", c);
            return (optstring[0] == ':') ? ':' : '?';
        } else {
            optarg = argv[optind++];
        }
    } else {
        optarg = NULL;
    }

    return (unsigned char)c;
}
#endif /* !GETOPT_HAVE_SYSTEM */

/* ---------- getopt_long (always) ---------- */

/* On Unix with system getopt, optarg/optind still come from libc.
 * We need declarations. */
#ifdef GETOPT_HAVE_SYSTEM
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
#endif

int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex)
{
    const char *arg;
    const char *eq;
    size_t namelen;
    int i;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    arg = argv[optind];

    /* Not a long option → fall back to getopt for -x style */
    if (arg[0] != '-' || arg[1] != '-' || arg[2] == '\0') {
        /* plain "-" or short options */
        if (arg[0] == '-' && arg[1] != '-')
            return getopt(argc, argv, optstring ? optstring : "");
        return -1;
    }

    /* "--" ends options */
    if (strcmp(arg, "--") == 0) {
        optind++;
        return -1;
    }

    arg += 2; /* skip -- */
    eq = strchr(arg, '=');
    namelen = eq ? (size_t)(eq - arg) : strlen(arg);

    if (!longopts) {
        if (opterr)
            fprintf(stderr, "unrecognized option '--%.*s'\n", (int)namelen, arg);
        optind++;
        return '?';
    }

    for (i = 0; longopts[i].name != NULL; i++) {
        if (strncmp(longopts[i].name, arg, namelen) == 0 &&
            longopts[i].name[namelen] == '\0') {
            optind++;
            optarg = NULL;

            if (longopts[i].has_arg == required_argument ||
                longopts[i].has_arg == optional_argument) {
                if (eq) {
                    optarg = (char *)(eq + 1);
                } else if (longopts[i].has_arg == required_argument) {
                    if (optind >= argc) {
                        if (opterr)
                            fprintf(stderr, "option '--%s' requires an argument\n",
                                    longopts[i].name);
                        return (optstring && optstring[0] == ':') ? ':' : '?';
                    }
                    optarg = argv[optind++];
                }
            } else if (eq) {
                if (opterr)
                    fprintf(stderr, "option '--%s' doesn't allow an argument\n",
                            longopts[i].name);
                return '?';
            }

            if (longindex)
                *longindex = i;

            if (longopts[i].flag) {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }
    }

    if (opterr)
        fprintf(stderr, "unrecognized option '--%.*s'\n", (int)namelen, arg);
    optind++;
    return '?';
}
