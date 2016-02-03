#define _GNU_SOURCE
#include <stdio.h>
#include "tsc.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

extern uint64_t start;


uint64_t inactive_periods(int num, uint64_t threshold, uint64_t *samples){

	start_counter();
	uint64_t ret = get_counter(), tmp;
	for (int i = 0; i < num; i++){
		start_counter();
		tmp = get_counter();
		printf("Active %d: start at %u, duration %u cycles\n", i, start, tmp);
	}
	return ret;
}


int main (int argc, char ** argv) {

	int num, length;
	cpu_set_t cpuset;
	uint64_t threshold;
	uint64_t *samples;
	length = num;
	samples = malloc(sizeof(uint64_t) * length);

	if (argc > 2) {
		fprintf(stderr, "%s\n", "Usage parta (optional) <num>");
		exit(0);
	}

	num = (argc == 2) ? atoi(argv[1]) : 1;

	CPU_SET(0, &cpuset);

	if (sched_setaffinity(getpid(), sizeof(cpuset), &cpuset) == -1)
		fprintf(stderr, "%s\n", "sched_setaffinity");

	inactive_periods(num, threshold, samples);

	return 0;
	
}