#include <stdint.h>

#ifndef TSC_H
#define TSC_H
extern void start_counter();
extern uint64_t get_counter();
#endif

void access_counter(unsigned *hi, unsigned *lo);
