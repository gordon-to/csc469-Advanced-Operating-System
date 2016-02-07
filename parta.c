#define _GNU_SOURCE
#include <stdio.h>
#include "tsc.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

extern uint64_t start;


uint64_t inactive_periods(int num, uint64_t threshold, uint64_t *samples){

	uint64_t ret = get_counter(), tmp = ret;
	int i;
	for (i = 0; i < num; i++){
		start_counter();
		while(get_counter() < threshold)
			
		printf("Active %d: start at %u, duration %u cycles\n", i, start, tmp);
		tmp = get_counter();
	}
	return ret;
}

void getcpu_freq(uint64_t * cpufreq){
	int microseconds = 1;
	start_counter();
	usleep(microseconds);
	*cpufreq = get_counter() * (100/microseconds);
}


int main (int argc, char ** argv) {

	int num, length;
	cpu_set_t cpuset;
	uint64_t threshold;
	uint64_t *samples;
	uint64_t *cpufreq;
	length = num;
	cpufreq = malloc(sizeof(uint64_t));
	samples = malloc(sizeof(uint64_t) * length);

	if (argc > 2) {
		fprintf(stderr, "%s\n", "Usage parta (optional) <num>");
		exit(0);
	}

	num = (argc == 2) ? atoi(argv[1]) : 1;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (sched_setaffinity(getpid(), sizeof(cpuset), &cpuset) == -1)
		fprintf(stderr, "%s\n", "sched_setaffinity");

	//getcpu frequency
	while(1){
		getcpu_freq(cpufreq);
		printf("%u\n", *cpufreq);
	}

	inactive_periods(num, threshold, samples);

	return 0;
	
}