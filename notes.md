## getopt
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

## Advanced I/O Function
### readv and writev Functions

```
#include <sys/uio.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

The `readv()` system call reads `iovcnt` buffers from the file associated with the file descriptor `fd` into the buffers described by `iov` ("scatter input").

The `writev()` system call writes `iovcnt` buffers of data described by `iov` to the file associated with the file descriptor `fd` ("gather output").

The  pointer  `iov`  points  to  an array of `iovec` structures, defined in `<sys/uio.h>` as:
```
struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};
```
The `readv()` system call works just like `read()`  except  that  multiple buffers are filled.

The  `writev()` system call works just like `write()` except that multiple buffers are written out.

Buffers are processed in array order.  This  means that `readv()` completely fills `iov[0]` before proceeding to `iov[1]`, and so on. Similarly, `writev()` writes out the entire contents of `iov[0]` before proceeding to `iov[1]`, and so on.

## libev
### WATCHER TYPES
#### ev_timer - relative and optionally repeating timeouts
##### Be smart about timeouts
1. Use a timer and stop, reinitialise and start it on activity. This is the most obvious, but not the most simple way: In the beginning, start the watcher:
```
ev_timer_init (timer, callback, 60., 0.);
ev_timer_start (loop, timer);
```
Then, each time there is some activity, "ev_timer_stop" it, initialise it and start it again:
```
ev_timer_stop (loop, timer);
ev_timer_set (timer, 60., 0.);
ev_timer_start (loop, timer);
```
This is relatively simple to implement, but means that each time there is some activity, libev will first have to remove the timer from its internal data structure and then add it again.

Libev tries to be fast, but it's still not a constant-time operation.

2. Use a timer and re-start it with "ev_timer_again" inactivity.
This is the easiest way, and involves using "ev_timer_again" instead of "ev_timer_start".

To implement this, configure an "ev_timer" with a "repeat" value of 60 and then call "ev_timer_again" at start and each time you successfully read or write some data. 

At start:
```
ev_init (timer, callback);
timer->repeat = 60.;
ev_timer_again (loop, timer);
```
Each time there is some activity:
```
ev_timer_again (loop, timer);
```
It is even possible to change the time-out on the fly, regardless of whether the watcher is active or not:
```
timer->repeat = 30.;
ev_timer_again (loop, timer);
```
This is slightly more efficient then stopping/starting the timer each time you want to modify its timeout value, as libev does not have to completely remove and re-insert the timer from/into its internal data structure.

### ANATOMY OF A WATCHER

In the following description, uppercase "TYPE" in names stands for the watcher type, e.g. "ev_TYPE_start" can mean "ev_timer_start" for timer watchers and "ev_io_start" for I/O watchers.

A watcher is an opaque structure that you allocate and register to record your interest in some event. To make a concrete example, imagine you want to wait for STDIN to become readable, you would create an "ev_io" watcher for that:
```
static void my_cb (struct ev_loop *loop, ev_io *w, int revents)
{
  ev_io_stop (w);
  ev_break (loop, EVBREAK_ALL);
}

struct ev_loop *loop = ev_default_loop (0);

ev_io stdin_watcher;

ev_init (&stdin_watcher, my_cb);
ev_io_set (&stdin_watcher, STDIN_FILENO, EV_READ);
ev_io_start (loop, &stdin_watcher);

ev_run (loop, 0);
```
Each watcher structure must be initialised by a call to "ev_init (watcher *, callback)", which expects a callback to be provided. This callback is invoked each time the event occurs (or, in the case of I/O watchers, each time the event loop detects that the file descriptor given is readable and/or writable).

To make the watcher actually watch out for events, you have to start it with a watcher-specific start function ("ev_TYPE_start (loop, watcher *)"), and you can stop watching for events at any time by calling the corresponding stop function ("ev_TYPE_stop (loop, watcher *)".

As long as your watcher is active (has been started but not stopped) you must not touch the values stored in it.

Each and every callback receives the event loop pointer as first, the registered watcher structure as second, and a bitset of received events as third argument.

## LD_DEBUG
Output verbose debugging information about the dynamic linker. If set to all prints all debugging information it has.


## make
```
-n, --just-print, --dry-run, --recon
     Print the commands that would be executed, but do not execute them.
```

## compilation and libraries
The compilation of a source code into an executable takes place in 4 stages, Preprocessing, Compilation, Assembly and linking. Let us consider the following C source code example before we discuss concepts further.
```
#include <stdio.h>

#define AVALUE 5

int main()
{
	float a = AVALUE;
	float b = 25;

	// calculate and print arithmetic operations
	printf("%f\n", a * b);
	printf("%f\n", a + b);
}
```
gcc:
```
-o <file> Place the output into <file>
```

Preprocessing

In the source code of our program above, the lines that begin with a # are preprocessor directives. Preprocessor directives are processed by the preprocessor and are never seen by the compiler. The preprocessor performs text substitutes, macro expansion, comment removal and file inclusion. The first preprocessor in our program above is #include <stdio.h>  which instructs the preprocessor to include the header file stdio.h into our source. The next processor directive #define AVALUE 5, defines a text substitute. Once the preprocessor has completed processing our source code, every occurrence of the string AVALUE is substituted by the string 5.

We can instruct GCC to stop processing the source code after preprocessing and to send the preprocessed file to stdout.

gcc:
```
-E Preprocess only; do not compile, assemble or link
gcc -E hello.c > hello.txt
```
The output hello.txt has 851 lines. This is because the preprocessor includes the contents of the header file stdio.h, which in turn includes several other header files. Notice that comments have been removed and text substitution has been done.

Compilation to Assembly Language

This part of the compilation process converts the preprocessed source code into assembly language.

gcc:
```

```