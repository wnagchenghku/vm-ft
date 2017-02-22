#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>

const int page_size = 4096;
int64_t nr_total_pages = 256 * 1024 * 2;
void* mem_head;

#define NUM_THREAD 1

struct thread_info {
	pthread_t thread_id;
	int       thread_num;
};

static void *thread_start(void *arg)
{
	int64_t i;
	char* var;
	for (;;)
	{
		for (i = 0; i < nr_total_pages; ++i)
		{
			var = mem_head;
			var += i * page_size;
			*var = 'a';
		}
	}
}

int main(int argc, char const *argv[])
{
	int64_t mem_size = nr_total_pages * page_size;
	int tnum, s;
	void *res;
	struct thread_info *tinfo;

	mem_head = malloc(mem_size);
	if (mem_head == NULL)
	{
		printf("Could not allocate guest memory.\n");
		exit(1);
	}

	tinfo = calloc(NUM_THREAD, sizeof(struct thread_info));
	if (tinfo == NULL)
		fprintf(stderr, "calloc");

	for (tnum = 0; tnum < NUM_THREAD; tnum++) {
               tinfo[tnum].thread_num = tnum + 1;

               s = pthread_create(&tinfo[tnum].thread_id, NULL, &thread_start, NULL);
               if (s != 0)
                   fprintf(stderr, "pthread_create");
	} 

	for (tnum = 0; tnum < NUM_THREAD; tnum++) {
		s = pthread_join(tinfo[tnum].thread_id, &res);
		if (s != 0)
                   fprintf(stderr, "pthread_join");
    }

	return 0;
}
