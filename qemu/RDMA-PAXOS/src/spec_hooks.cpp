#include <string>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include "include/rsm-interface.h"

#define dprintf(fmt...)

typedef int (*main_type)(int, char**, char**);

struct arg_type
{
	char **argv;
	int (*main_func) (int, char **, char **);
};

main_type saved_init_func = NULL;
void tern_init_func(int argc, char **argv, char **env)
{
	dprintf("%04d: __tern_init_func() called.\n", (int) pthread_self());
	if(saved_init_func)
		saved_init_func(argc, argv, env);

	printf("tern_init_func is called\n");

	char* config_path = getenv("config_path");

	char* proxy_log_dir = NULL;
	proxy_init(config_path, proxy_log_dir);
}

typedef void (*fini_type)(void*);
fini_type saved_fini_func = NULL;

extern "C" int my_main(int argc, char **pt, char **aa)
{
	int ret;
	arg_type *args = (arg_type*)pt;
	dprintf("%04d: __libc_start_main() called.\n", (int) pthread_self());
	ret = args->main_func(argc, args->argv, aa);
	return ret;
}

extern "C" int __libc_start_main(
	void *func_ptr,
	int argc,
	char* argv[],
	void (*init_func)(void),
	void (*fini_func)(void),
	void (*rtld_fini_func)(void),
	void *stack_end)
{
	typedef void (*fnptr_type)(void);
	typedef int (*orig_func_type)(void *, int, char *[], fnptr_type,
		fnptr_type, fnptr_type, void*);
	orig_func_type orig_func;
	arg_type args;

	void * handle;
	int ret;

	// Get lib path.
	Dl_info dli;
	dladdr((void *)dlsym, &dli);
	std::string libPath = dli.dli_fname;
	libPath = dli.dli_fname;
	size_t lastSlash = libPath.find_last_of("/");
	libPath = libPath.substr(0, lastSlash);
	libPath += "/libc.so.6";
	libPath = "/lib/x86_64-linux-gnu/libc.so.6";
	if(!(handle=dlopen(libPath.c_str(), RTLD_LAZY))) {
		puts("dlopen error");
		abort();
	}

	orig_func = (orig_func_type) dlsym(handle, "__libc_start_main");

	if(dlerror()) {
		puts("dlerror");
		abort();
	}

	dlclose(handle);

	dprintf("%04d: __libc_start_main is hooked.\n", (int) pthread_self());

	args.argv = argv;
	args.main_func = (main_type)func_ptr;
	saved_init_func = (main_type)init_func;

	saved_fini_func = (fini_type)rtld_fini_func;

	ret = orig_func((void*)my_main, argc, (char**)(&args), (fnptr_type)tern_init_func, (fnptr_type)fini_func, rtld_fini_func, stack_end);

	return ret;
}