#define _GNU_SOURCE
#include <stdio.h>
#include "tsc.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

uint64_t CPUFREQ;

uint64_t inactive_periods(int num, uint64_t threshold, uint64_t *samples){

	int i;
	start_counter();
	uint64_t start, prev, next;
	start = get_counter();
	prev = start;

	for (i = 0; i < num; i++){
		while(next = get_counter()){
			if ((next - prev) > threshold){
				printf("Active %d: start at %u, duration %u cycles (%u ms)\n", i, prev, next - prev, (next-prev)/CPUFREQ);
			}
		}

	}
	return start;
}

uint64_t getcpu_freq(int microseconds){
	//gets cpu freq
	int i, cpu_k_polls;
	start_counter();
	uint64_t prev;
	uint64_t sum = 0;
	cpu_k_polls = 10;
	for(i=0;i<cpu_k_polls;i++){
		prev = get_counter();
		usleep(microseconds);
		sum += (get_counter() - prev) * (1000000/microseconds); 
	}
	return sum/cpu_k_polls;
}


int main (int argc, char ** argv) {

	int num;
	cpu_set_t cpuset;
	uint64_t threshold;
	uint64_t *samples;

	if (argc > 2) {
		fprintf(stderr, "%s\n", "Usage parta (optional) <num>");
		exit(0);
	}

	threshold = 0;
	num = (argc == 2) ? atoi(argv[1]) : 1;

	int microseconds;
	microseconds = 100000;

	samples = malloc(sizeof(uint64_t) * num * 2);

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (sched_setaffinity(getpid(), sizeof(cpuset), &cpuset) == -1)
		fprintf(stderr, "%s\n", "sched_setaffinity");

	//getcpu frequency
	CPUFREQ = getcpu_freq(microseconds);
	

	// inactive_periods(num, tmhreshold, samples);

	return 0;
	
}