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
	uint64_t start, prev, next, difference, summ, start_counter;
	double ms_time;
	char active;
	active = 1;
	difference = 0;

	for (i = 0; i < num*2; i++) {
		summ = difference;
		start = get_counter();
		prev = start;
		while(next = get_counter()) {
			difference = next - prev;
			ms_time = ((double) (difference * 100)/ (double) CPUFREQ);
			if (difference > threshold && active){
				printf("Active %d: start at, duration %lu cycles (%f ms)\n", i/2, start, summ, ms_time);
				samples[i] = start;
				samples[i+1] = prev;
				active = 0;
				break;
			} else if (difference < threshold && !active){
				printf("Inactive %d: start at %lu, duration %lu cycles (%f ms)\n", i/2, start, summ, ms_time);
				active = 1;
				break;
			} else {
				summ += difference;
			}
			prev = next;
		}
	}

	return samples[0];
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

	if (argc != 2) {
		fprintf(stderr, "%s\n", "Usage: parta num");
		exit(0);
	}

	threshold = 2300;
	num = atoi(argv[1]);

	int microseconds;
	microseconds = 100000;

	samples = malloc(sizeof(uint64_t) * num * 2);

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (sched_setaffinity(getpid(), sizeof(cpuset), &cpuset) == -1)
		fprintf(stderr, "%s\n", "sched_setaffinity");

	//getcpu frequency
	CPUFREQ = getcpu_freq(microseconds);
	printf("%u mHz\n", CPUFREQ/1000000);

	inactive_periods(num, threshold, samples);
	free(samples);
	return 0;
	
}