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

/* ================================================================== */
/* Helper function */

#include <string.h>

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

/* ================================================================== */

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

## Source Code to Executable
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

### Preprocessing

In the source code of our program above, the lines that begin with a # are preprocessor directives. Preprocessor directives are processed by the preprocessor and are never seen by the compiler. The preprocessor performs text substitutes, macro expansion, comment removal and file inclusion. The first preprocessor in our program above is #include <stdio.h>  which instructs the preprocessor to include the header file stdio.h into our source. The next processor directive #define AVALUE 5, defines a text substitute. Once the preprocessor has completed processing our source code, every occurrence of the string AVALUE is substituted by the string 5.

We can instruct GCC to stop processing the source code after preprocessing and to send the preprocessed file to stdout.
```
gcc -E hello.c > hello.txt

gcc options:
    -E                       Preprocess only; do not compile, assemble or link
```
The output hello.txt has 851 lines. This is because the preprocessor includes the contents of the header file stdio.h, which in turn includes several other header files. Notice that comments have been removed and text substitution has been done.

### Compilation to Assembly Language
This part of the compilation process converts the preprocessed source code into assembly language.
```
gcc options:
  -S                       Compile only; do not assemble or link
```

### Assembly
This stage of the compilation process assembles the assembly language program generated into a machine language object file (.o).
```
gcc options:
  -c                       Compile and assemble, but do not link
```

When writing a program, the source code for various parts or functions of the program may be defined in separate source files. Since each source file is compiled into a separate object file, the object file produced from one source file may have a dependency on another object file. The function of the linker is to merge those object files so that any such dependencies are resolved.

## Libraries
### Static Libraries
We will create a small static library as an example to illustrate the linking process. First our main program test_link.c.
```
#include <stdio.h>

float cubed(float a);
float powerfour(float a);

int main()
{
	float x = 23.1415;
	float y = cubed(x);
	printf("%f\n", y);
	y = powerfour(x);
	printf("%f\n", y);

	return 0;
}
```
Notice that the function cubed and powerfour() are declared but not defined. The definition of cubed() and powerfour() will be placed in the next two files libcubed.c and libpowerfour.c. Below is the listing of libcubed.c.
```
float cubed(float a)
{
	return a * a * a;
}
```
And libpowerfour.c.
```
float powerfour(float a)
{
	return a * a * a * a;
}
```
To compile libcubed.c and libpowerfour.c as an object files, pass the -c option to gcc. This stops GCC from linking the object file into an executable and produces the object files libcubed.o and libpowerfour.o.
```
gcc -c libpowerfour.c libcubed.c
```
We will now combine libcubed.o and libpowerfour.o into a single static library.
```
ar rs libmymath.a libcubed.o libpowerfour.o
```
Let us try compiling the main program test_link.c without specifying the library object.
```
$ gcc -o test_link test_link.c 
/tmp/ccE11S3R.o: In function `main':
test_link.c:(.text+0x1d): undefined reference to `cubed'
test_link.c:(.text+0x4f): undefined reference to `powerfour'
collect2: error: ld returned 1 exit status
```
Now include the library file.
```
gcc -o test_link test_link.c -L. libmymath.a
```
Note the usage of the ‘-L’ flag - this flag tells the linker that libraries might be found in the given directory, in addition to the standard locations where the compiler looks for system libraries.

### dynamic libraries
As we described earlier, including static libraries while compiling programs actually merges the object files during the linking stage to produce a complete executable. There can be disadvantages to this. If there are several library files or they are very large, every executable that is generated by linking with these libraries will have a copy of the library code embedded in the executable. This makes the executable large. And when an executable is run, the executable file is actually mapped into memory before it is executed. This means it also occupies an unnecessarily large portion of memory. The solution to this is dynamic libraries.

It makes the executable smaller since they only contain references to and not the actual code the library functions. Also, only one copy of the code of a dynamic library needs to be maintained in main memory, and the operating system makes sure that every running process that refers to this library has its references resolved at run time.

The process of compiling the functions in the example above as a dynamic library consists of two steps.
```
gcc -fPIC -c libcubed.c libpowerfour.c
gcc -shared -Wl,-soname,libmymath.so -o libmymath.so libcubed.o libpowerfour.o
```
The -shared option tells gcc to produce a shared object file. -WI,soname,libmymath.so specifies the library soname.

Now, to link our main program to the dynamic libraries, we must compile it with
```
gcc -o test_link test_link.c -L. libmymath.so
```
The linker will look for the file ‘libmymath.so’ in the current directory (-L.), and link it to the program, but will place its object files inside the resulting executable file, ’test_link’.

On running the executable, instead of getting the expected output, we get the error.
```
$ ./test_link 
./test_link: error while loading shared libraries: libmymath.so: cannot open shared object file: No such file or directory
```
Normally, the system’s dynamic loader looks for shared libraries in some system specified directories (/lib and /usr/lib). When we build a new shared library that is not part of the system, we can use the LD_LIBRARY_PATH environment variable to tell the dynamic loader to look in other directories.

## Linking order
The linker checks each file in turn. If it is a library, the linker checks to see if any symbols referenced (i.e. used) in the previous object files but not defined (i.e. contained) in them, are in the library.
```
$ gcc -o test_link test_link.c -L. libmymath.so
$ gcc -o test_link -L. libmymath.so test_link.c
/tmp/ccWXGZDx.o: In function `main':
test_link.c:(.text+0x1d): undefined reference to `cubed'
test_link.c:(.text+0x4f): undefined reference to `powerfour'
collect2: error: ld returned 1 exit status
```
```
--as-needed
--no-as-needed
```
This option affects ELF DT_NEEDED tags for dynamic libraries mentioned on the command line after the --as-needed option. Normally the linker will add a DT_NEEDED tag for each dynamic library mentioned on the command line, regardless of whether the library is actually needed or not. --as-needed causes a DT_NEEDED tag to only be emitted for a library that at that point in the link satisfies a non-weak undefined symbol reference from a regular object file or, if the library is not found in the DT_NEEDED lists of other libraries, a non-weak undefined symbol reference from another dynamic library. Object files or libraries appearing on the command line after the library in question do not affect whether the library is seen as needed. This is similar to the rules for extraction of object files from archives. --no-as-needed restores the default behaviour.
```
$ gcc -o test_link test_link.c -L. libmymath.so
$ readelf -d test_link

Dynamic section at offset 0xe18 contains 25 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libmymath.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000c (INIT)               0x4005b8
 0x000000000000000d (FINI)               0x400824
 0x0000000000000019 (INIT_ARRAY)         0x600e00
 0x000000000000001b (INIT_ARRAYSZ)       8 (bytes)
 0x000000000000001a (FINI_ARRAY)         0x600e08
 0x000000000000001c (FINI_ARRAYSZ)       8 (bytes)
 0x000000006ffffef5 (GNU_HASH)           0x400298
 0x0000000000000005 (STRTAB)             0x400420
 0x0000000000000006 (SYMTAB)             0x4002d0
 0x000000000000000a (STRSZ)              202 (bytes)
 0x000000000000000b (SYMENT)             24 (bytes)
 0x0000000000000015 (DEBUG)              0x0
 0x0000000000000003 (PLTGOT)             0x601000
 0x0000000000000002 (PLTRELSZ)           120 (bytes)
 0x0000000000000014 (PLTREL)             RELA
 0x0000000000000017 (JMPREL)             0x400540
 0x0000000000000007 (RELA)               0x400528
 0x0000000000000008 (RELASZ)             24 (bytes)
 0x0000000000000009 (RELAENT)            24 (bytes)
 0x000000006ffffffe (VERNEED)            0x400508
 0x000000006fffffff (VERNEEDNUM)         1
 0x000000006ffffff0 (VERSYM)             0x4004ea
 0x0000000000000000 (NULL)               0x0
```

```
$ gcc -Wl,--no-as-needed -o test_link -L. libmymath.so -libverbs test_link.c
$ readelf -d test_link

Dynamic section at offset 0xe08 contains 26 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libmymath.so]
 0x0000000000000001 (NEEDED)             Shared library: [libibverbs.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000c (INIT)               0x4005c8
 0x000000000000000d (FINI)               0x400834
 0x0000000000000019 (INIT_ARRAY)         0x600df0
 0x000000000000001b (INIT_ARRAYSZ)       8 (bytes)
 0x000000000000001a (FINI_ARRAY)         0x600df8
 0x000000000000001c (FINI_ARRAYSZ)       8 (bytes)
 0x000000006ffffef5 (GNU_HASH)           0x400298
 0x0000000000000005 (STRTAB)             0x400420
 0x0000000000000006 (SYMTAB)             0x4002d0
 0x000000000000000a (STRSZ)              218 (bytes)
 0x000000000000000b (SYMENT)             24 (bytes)
 0x0000000000000015 (DEBUG)              0x0
 0x0000000000000003 (PLTGOT)             0x601000
 0x0000000000000002 (PLTRELSZ)           120 (bytes)
 0x0000000000000014 (PLTREL)             RELA
 0x0000000000000017 (JMPREL)             0x400550
 0x0000000000000007 (RELA)               0x400538
 0x0000000000000008 (RELASZ)             24 (bytes)
 0x0000000000000009 (RELAENT)            24 (bytes)
 0x000000006ffffffe (VERNEED)            0x400518
 0x000000006fffffff (VERNEEDNUM)         1
 0x000000006ffffff0 (VERSYM)             0x4004fa
 0x0000000000000000 (NULL)               0x0
```

## QEMU configure
```
configure options:
--extra-cflags=CFLAGS    append extra C compiler flags QEMU_CFLAGS
--extra-ldflags=LDFLAGS  append extra linker flags LDFLAGS
```

## tcpdump
```
Syntax: tcpdump [options] [filter expression]
```
Capture only UDP packets with destination port 53
```
tcpdump "udp dst port 53"
```
Running tcpdump requires superuser/administrator privileges.