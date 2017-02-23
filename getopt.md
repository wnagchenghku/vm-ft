the *main* function gets invoked with a count of these arguments as the first parameter and the argument list itself as the second parameter.
For this example, let’s just print each element of the argument list.
```
#include <stdio.h>

int
main(int argc, char **argv)
{
	int i;

	printf("argc = %d\n", argc);
	for (i=0; i<argc; i++) 
		printf("arg[%d] = \"%s\"\n", i, argv[i]);
}
```
Compile this program via:
```
gcc -o echoargs echoargs.c
```
Run the program:
```
./echo does she "weigh the same" as\ a duck?
```
You should see:
```
argc = 7
arg[0] = "./echo"
arg[1] = "does"
arg[2] = "she"
arg[3] = "weigh the same"
arg[4] = "as"
arg[5] = "a"
arg[6] = "duck?"
```

```
#include <stdio.h>
#include <stdlib.h>

int debug = 0;

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int c, err = 0; 
	int mflag=0, pflag=0, fflag=0;
	char *sname = "default_sname", *fname;
	static char usage[] = "usage: %s [-dmp] -f fname [-s sname] name [name ...]\n";

	while ((c = getopt(argc, argv, "df:mps:")) != -1)
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'm':
			mflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'f':
			fflag = 1;
			fname = optarg;
			break;
		case 's':
			sname = optarg;
			break;
		case '?':
			err = 1;
			break;
		}
	if (fflag == 0) {	/* -f was mandatory */
		fprintf(stderr, "%s: missing -f option\n", argv[0]);
		fprintf(stderr, usage, argv[0]);
		exit(1);
	} else if ((optind+1) > argc) {	
		/* need at least one argument (change +1 to +2 for two, etc. as needeed) */

		printf("optind = %d, argc=%d\n", optind, argc);
		fprintf(stderr, "%s: missing name\n", argv[0]);
		fprintf(stderr, usage, argv[0]);
		exit(1);
	} else if (err) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	/* see what we have */
	printf("debug = %d\n", debug);
	printf("pflag = %d\n", pflag);
	printf("mflag = %d\n", mflag);
	printf("fname = \"%s\"\n", fname);
	printf("sname = \"%s\"\n", sname);
	
	if (optind < argc)	/* these are the arguments after the command-line options */
		for (; optind < argc; optind++)
			printf("argument: \"%s\"\n", argv[optind]);
	else {
		printf("no arguments left to process\n");
	}
	exit(0);
}
```
There are things you need to know about using *getopt*:
* Declare extern char *optarg; extern int optind; in your function. These are two external variables that getopt uses. The first, optarg, is used when parsing options that take a name as a parameter. In those cases, it contains a pointer to that parameter.
* getopt takes three parameters. The first two are straight from main: the argument count and argument list. The third is the one that defines the syntax. It’s a string that contains all the characters that make up the valid line options. Any options that requires a parameter alongside it is suffixed by a colon (:).
* getopt is called repeatedly. Each time it is called, it return the next command-line option that it found. If theres a follow-on parameter, it is stored in optarg. If there are no more command-line options found then getopt returns a -1. We usually program calls to getopt in a while loop with a switch statement within with a case for each option.
* It’s up to you to enforce which options are mandatory and which are optional. You may do this by setting variables and then checking them when getopt is done processing options.

For example, we want -f to be mandatory, so we set:
```
fflag = 1;
```
when we encounter the -f option and then, after getopt is done, we check:
```
if (fflag == 0) {
	fprintf(stderr, "%s: missing -f option\n", argv[0]);
	fprintf(stderr, usage, argv[0]);
	exit(1)
}
```

```
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const char *get_opt_name(char *buf, int buf_size, const char *p, char delim)
{
    char *q;

    q = buf;
    while (*p != '\0' && *p != delim) {
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

const char *get_opt_value(char *buf, int buf_size, const char *p)
{
    char *q;

    q = buf;
    while (*p != '\0') {
        if (*p == ',') {
            if (*(p + 1) != ',')
                break;
            p++;
        }
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

static void opts_do_parse(const char *params)
{
    char option[128], value[1024];
    const char *p,*pe,*pc;

    for (p = params; *p != '\0'; p++) {
        pe = strchr(p, '=');
        pc = strchr(p, ',');
        if (!pe || (pc && pc < pe)) {
            /* found "foo,more" */
            p = get_opt_name(option, sizeof(option), p, ',');
        } else {
            /* found "foo=bar,more" */
            p = get_opt_name(option, sizeof(option), p, '=');
            if (*p != '=') {
                break;
            }
            p++;
            p = get_opt_value(value, sizeof(value), p);
        }

        if (*p != ',') {
            break;
        }
    }
}

typedef struct QEMUOption {
    const char *name;
} QEMUOption;

static const QEMUOption qemu_options[] = {
    { "netdev" },
    { "chardev" },
    { NULL },
};

static const QEMUOption *lookup_opt(int argc, char **argv,
                                    const char **poptarg, int *poptind)
{
    const QEMUOption *popt;
    int optind = *poptind;
    char *r = argv[optind];
    const char *optarg;

    optind++;
    /* Treat --foo the same as -foo.  */
    if (r[1] == '-')
        r++;
    popt = qemu_options;
    for(;;) {
        if (!popt->name) {
            fprintf(stderr, "invalid option\n");
            exit(1);
        }
        if (!strcmp(popt->name, r + 1))
            break;
        popt++;
    }

    optarg = argv[optind++];

    *poptarg = optarg;
    *poptind = optind;

    return popt;
}

int main(int argc, char **argv)
{
    const char *optarg;
    int optind;

    optind = 1;
    while (optind < argc) {
        if (argv[optind][0] != '-') {
            optind++;
        } else {
            const QEMUOption *popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            // switch () {
            // }
        }
    }

    return 0;
}
```
./a.out -netdev tap,id=hn0,vhost=off,script=/etc/qemu-ifup,downscript=/etc/qemu-ifdown -chardev socket,id=red0,host=127.0.0.1,port=9003