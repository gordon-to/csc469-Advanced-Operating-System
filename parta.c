#include <stdio.h>
#include "tsc.h"

#ifndef u_int64_t
#define u_int64_t uint64_t
#endif

u_int64_t inactive_periods(int num, u_int64_t threshold, u_int64_t *samples){

}


int main () {

	start_counter();

	u_int64_t tmp = get_counter();

	printf("%u\n", tmp);	

	return 0;
}