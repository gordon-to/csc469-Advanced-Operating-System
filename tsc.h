#include <stdint.h>

#ifndef TSC_H
#define TSC_H
uint64_t start;
extern void start_counter();
extern uint64_t get_counter();
#endif

void access_counter(unsigned *hi, unsigned *lo);
