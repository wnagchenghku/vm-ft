#include <stdio.h>
#include <time.h>

#define LOOP 50000000L

int main(){
	long i; 
	FILE *f = fopen("file.txt", "w");


	struct timespec tstart={0,0}, tend={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    


	for (i = 0; i < LOOP; i++){
		fprintf(f, "a");
		fflush(f);
	}
	clock_gettime(CLOCK_MONOTONIC, &tend);
    printf("some_long_computation took about %.5f seconds\n",
           ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - 
           ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec));



}